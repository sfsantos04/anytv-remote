import Foundation
import SwiftUI
import UIKit

/// One launchable app on the TV. `icon` is either a direct image URL (Roku serves
/// it at /query/icon/<id>, webOS hands out its own path) or a Play Store page that
/// the icon store scrapes for the official artwork and title.
struct TVApp: Identifiable, Hashable {
    let id: String
    let name: String
    let icon: URL?

    /// Deterministic colour for the letter tile shown while the icon loads, or
    /// forever when the app is not on the store (sideloaded APKs).
    var tint: UIColor {
        var hash: UInt32 = 2166136261
        for byte in id.utf8 {
            hash = (hash ^ UInt32(byte)) &* 16777619
        }
        let hue = CGFloat(hash % 360) / 360.0
        return UIColor(hue: hue, saturation: 0.45, brightness: 0.72, alpha: 1)
    }

    var initials: String {
        let letters = name.split(separator: " ").prefix(2).compactMap { $0.first }
        let text = String(letters).uppercased()
        return text.isEmpty ? "?" : text
    }
}

/// Loads and caches app icons: memory first, then disk, then the network, and as
/// a last resort the artwork extracted from the APK by the TV itself.
final class IconStore: ObservableObject {
    @Published private(set) var images: [String: UIImage] = [:]
    @Published private(set) var titles: [String: String] = [:]

    private var inFlight: Set<String> = []
    private let directory: URL
    private let client: TVClient?

    init(client: TVClient? = nil) {
        self.client = client
        let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        directory = base.appendingPathComponent("AppIcons", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    private func fileURL(_ id: String) -> URL {
        directory.appendingPathComponent(Self.fileKey(id) + ".png")
    }

    func displayName(for app: TVApp) -> String {
        if let official = titles[app.id], !official.isEmpty { return official }
        return app.name
    }

    func image(for app: TVApp) -> UIImage? { images[app.id] }

    @MainActor
    func load(_ app: TVApp) async {
        guard images[app.id] == nil, !inFlight.contains(app.id) else { return }
        inFlight.insert(app.id)
        defer { inFlight.remove(app.id) }

        // icone embutido no app (marcas conhecidas): instantaneo e sem rede
        if let bundled = UIImage(named: app.id) {
            images[app.id] = bundled
            return
        }

        let file = fileURL(app.id)
        if let data = try? Data(contentsOf: file), let image = UIImage(data: data) {
            images[app.id] = image
            replaceTitleWithCache(app.id)
            return
        }

        if let source = app.icon, source.host == "play.google.com" {
            if let scraped = await Self.scrapePlay(source) {
                if let title = scraped.title, !title.isEmpty { cache(title: title, for: app.id) }
                if let data = await Self.download(scraped.icon), let image = UIImage(data: data) {
                    images[app.id] = image
                    try? data.write(to: file)
                    return
                }
            }
            // Not on the store (sideloaded IPTV builds): ask the TV for the icon.
            _ = await loadFromDevice(app)
            return
        }

        if let source = app.icon, let data = await Self.download(source), let image = UIImage(data: data) {
            images[app.id] = image
            try? data.write(to: file)
            return
        }
        _ = await loadFromDevice(app)
    }

    @MainActor
    private func loadFromDevice(_ app: TVApp) async -> Bool {
        guard let client,
              let data = try? await client.appIcon(app.id),
              let image = UIImage(data: data) else { return false }
        images[app.id] = image
        try? data.write(to: fileURL(app.id))
        return true
    }

    private func cache(title: String, for id: String) {
        titles[id] = title
        try? title.write(to: directory.appendingPathComponent(Self.fileKey(id) + ".txt"),
                         atomically: true, encoding: .utf8)
    }

    private static func download(_ url: URL) async -> Data? {
        var request = URLRequest(url: url)
        request.timeoutInterval = 12
        guard let (data, response) = try? await URLSession.shared.data(for: request),
              (response as? HTTPURLResponse)?.statusCode ?? 0 == 200 else { return nil }
        return data
    }

    private func replaceTitleWithCache(_ id: String) {
        let file = directory.appendingPathComponent(Self.fileKey(id) + ".txt")
        if let cached = try? String(contentsOf: file, encoding: .utf8), !cached.isEmpty {
            titles[id] = cached
        }
    }

    /// Play only exposes the name and the artwork inside the page metadata; there
    /// is no public JSON endpoint without an API key.
    private static func scrapePlay(_ url: URL) async -> (icon: URL, title: String?)? {
        var request = URLRequest(url: url)
        request.timeoutInterval = 12
        request.setValue("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15",
                         forHTTPHeaderField: "User-Agent")
        request.setValue("pt-BR", forHTTPHeaderField: "Accept-Language")
        guard let (data, response) = try? await URLSession.shared.data(for: request),
              (response as? HTTPURLResponse)?.statusCode == 200,
              let html = String(data: data, encoding: .utf8) else { return nil }

        func meta(_ property: String) -> String? {
            let pattern = "property=\"\(property)\" content=\"([^\"]+)\""
            guard let regex = try? NSRegularExpression(pattern: pattern) else { return nil }
            let range = NSRange(html.startIndex..<html.endIndex, in: html)
            guard let match = regex.firstMatch(in: html, range: range),
                  let found = Range(match.range(at: 1), in: html) else { return nil }
            return String(html[found])
        }

        guard let iconText = meta("og:image"), let icon = URL(string: iconText) else { return nil }
        var title = meta("og:title")
        for separator in [" – ", " - ", " | "] {
            if let text = title, let cut = text.range(of: separator) {
                title = String(text[..<cut.lowerBound])
            }
        }
        return (icon, title)
    }

    private static func fileKey(_ id: String) -> String {
        var hash: UInt64 = 1469598103934665603
        for byte in id.utf8 {
            hash = (hash ^ UInt64(byte)) &* 1099511628211
        }
        return String(hash, radix: 16)
    }
}

struct AppTile: View {
    let app: TVApp
    @ObservedObject var store: IconStore
    let canLaunch: Bool
    let action: () -> Void

    private var tint: Color { Color(uiColor: app.tint) }

    var body: some View {
        Button(action: action) {
            VStack(spacing: 7) {
                ZStack {
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .fill(Color(uiColor: .secondarySystemBackground))
                    if let image = store.image(for: app) {
                        Image(uiImage: image)
                            .resizable()
                            .scaledToFit()
                            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                            .padding(5)
                    } else {
                        RoundedRectangle(cornerRadius: 16, style: .continuous)
                            .fill(tint.opacity(0.18))
                        Text(app.initials)
                            .font(.system(size: 30, weight: .semibold, design: .rounded))
                            .foregroundStyle(tint)
                    }
                }
                .frame(width: 104, height: 104)
                .overlay(
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .stroke(Color.primary.opacity(0.06), lineWidth: 1)
                )
                Text(store.displayName(for: app))
                    .font(.caption)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(.primary)
                    .frame(width: 108, height: 30, alignment: .top)
            }
        }
        .buttonStyle(.plain)
        .disabled(!canLaunch)
        .task { await store.load(app) }
    }
}

struct AppsSheet: View {
    let client: TVClient
    let canLaunch: Bool

    @Environment(\.dismiss) private var dismiss
    @StateObject private var store: IconStore
    @State private var apps: [TVApp] = []
    @State private var loading = true
    @State private var message = ""

    init(client: TVClient, canLaunch: Bool) {
        self.client = client
        self.canLaunch = canLaunch
        _store = StateObject(wrappedValue: IconStore(client: client))
    }

    private let columns = [GridItem(.adaptive(minimum: 108, maximum: 118), spacing: 14)]

    var body: some View {
        NavigationStack {
            ScrollView {
                if !message.isEmpty {
                    Text(message)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal)
                        .padding(.top, 6)
                }
                LazyVGrid(columns: columns, spacing: 18) {
                    ForEach(apps) { app in
                        AppTile(app: app, store: store, canLaunch: canLaunch) {
                            launch(app)
                        }
                    }
                }
                .padding(16)
            }
            .overlay {
                if loading {
                    VStack(spacing: 10) {
                        ProgressView()
                        Text("Lendo apps da TV...").font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle(apps.isEmpty ? "Apps da TV" : "Apps da TV (\(apps.count))")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Fechar") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button { Task { await load() } } label: { Image(systemName: "arrow.clockwise") }
                }
            }
            .task { await load() }
        }
    }

    private func load() async {
        loading = true
        message = ""
        do {
            let list = try await client.apps()
            apps = list
            loading = false
            if list.isEmpty { message = "A TV não devolveu nenhum app." }
        } catch {
            loading = false
            message = "Falhou: \(error.localizedDescription)"
        }
    }

    private func launch(_ app: TVApp) {
        guard canLaunch else {
            message = "Este aparelho não permite abrir app pelo controle."
            return
        }
        Task {
            do {
                try await client.launch(app.id)
                message = "Abrindo \(store.displayName(for: app)) na TV."
            } catch {
                message = "Falhou: \(error.localizedDescription)"
            }
        }
    }
}
