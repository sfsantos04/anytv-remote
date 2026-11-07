import SwiftUI
import UIKit

struct ScreenSheet: View {
    let client: TVClient
    @Environment(\.dismiss) private var dismiss
    @State private var image: UIImage?
    @State private var message = ""
    @State private var auto = true
    @State private var loading = false

    private let timer = Timer.publish(every: 2.0, on: .main, in: .common).autoconnect()

    var body: some View {
        NavigationStack {
            VStack(spacing: 10) {
                if let image {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFit()
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                } else {
                    RoundedRectangle(cornerRadius: 10)
                        .fill(Color.secondary.opacity(0.15))
                        .frame(height: 220)
                        .overlay {
                            if loading { ProgressView() } else { Text("Sem imagem").foregroundStyle(.secondary) }
                        }
                }
                if !message.isEmpty {
                    Text(message).font(.footnote).foregroundStyle(.secondary)
                }
                Toggle("Atualizar a cada 2 s", isOn: $auto)
                    .padding(.horizontal)
            }
            .padding()
            .navigationTitle("Tela da TV")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Fechar") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button { Task { await refresh() } } label: { Image(systemName: "arrow.clockwise") }
                }
            }
            .task { await refresh() }
            .onReceive(timer) { _ in
                guard auto, !loading else { return }
                Task { await refresh() }
            }
        }
    }

    private func refresh() async {
        guard !loading else { return }
        loading = true
        do {
            let data = try await client.screenshot()
            if let loaded = UIImage(data: data) {
                image = loaded
                message = "\(Int(loaded.size.width))x\(Int(loaded.size.height)) - \(data.count / 1024) KiB"
            } else {
                message = "A TV devolveu \(data.count) bytes que nao sao um PNG."
            }
        } catch {
            message = "Falhou: \(error.localizedDescription)"
        }
        loading = false
    }
}
