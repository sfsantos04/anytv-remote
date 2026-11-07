import SwiftUI
import UIKit

/// Live screen: the TV encodes H.264 and the phone decodes it with VideoToolbox.
/// When the device has no encoder (or the user turns it off) the sheet falls back
/// to PNG screenshots every couple of seconds.
struct ScreenSheet: View {
    let client: TVClient
    @Environment(\.dismiss) private var dismiss

    @StateObject private var player = H264StreamPlayer()
    @State private var image: UIImage?
    @State private var message = ""
    @State private var live = true
    @State private var auto = true
    @State private var loading = false
    @State private var streamTask: Task<Void, Never>?

    private let timer = Timer.publish(every: 2.0, on: .main, in: .common).autoconnect()

    init(client: TVClient) {
        self.client = client
        _live = State(initialValue: client.supports(Cap.screen))
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 12) {
                if live { liveArea } else { snapshotArea }
                if !message.isEmpty {
                    Text(message).font(.footnote).foregroundStyle(.secondary)
                }
                controls
                Text("O áudio não vem junto: o encoder do sistema só entrega vídeo. Em Android 11+ o caminho é o servidor do scrcpy; neste Android 7 seria preciso root com captura ALSA.")
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
                    .padding(.horizontal)
            }
            .padding()
            .navigationTitle("Tela da TV")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Fechar") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        if live { restartLive() } else { Task { await refresh() } }
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                }
            }
            .task {
                if live { startLive() } else { await refresh() }
            }
            .onDisappear { stopLive() }
            .onChange(of: live) { nowLive in
                if nowLive { startLive() } else { stopLive(); Task { await refresh() } }
            }
            .onReceive(timer) { _ in
                guard !live, auto, !loading else { return }
                Task { await refresh() }
            }
        }
    }

    private var liveArea: some View {
        LiveVideoView(player: player)
            .aspectRatio(16.0 / 9.0, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(alignment: .topLeading) { statsBadge }
    }

    private var statsBadge: some View {
        let frames = player.frames
        let rate = Int(player.kilobytesPerSecond)
        return Text("\(frames) quadros · \(rate) KB/s")
            .font(.caption2.monospaced())
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(.ultraThinMaterial, in: Capsule())
            .padding(8)
    }

    private var snapshotArea: some View {
        Group {
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
                        if loading { ProgressView() } else {
                            Text("Sem imagem").foregroundStyle(.secondary)
                        }
                    }
            }
        }
    }

    private var controls: some View {
        VStack(spacing: 8) {
            if client.supports(Cap.screen) {
                Toggle("Tela ao vivo (H.264)", isOn: $live)
            }
            if !live {
                Toggle("Atualizar a cada 2 s", isOn: $auto)
            }
        }
        .padding(.horizontal)
    }

    private func restartLive() {
        stopLive()
        startLive()
    }

    private func startLive() {
        guard streamTask == nil else { return }
        player.reset()
        message = "Abrindo o encoder da TV..."
        streamTask = Task {
            while !Task.isCancelled {
                do {
                    try await client.streamStart(seconds: 170)
                } catch {
                    message = "Falhou: \(error.localizedDescription)"
                    break
                }
                message = "Ao vivo"
                var ended = false
                while !Task.isCancelled, !ended {
                    do {
                        let chunk = try await client.streamRead()
                        if let chunk {
                            if chunk.isEmpty { continue }
                            player.feed(chunk)
                        } else {
                            ended = true
                        }
                    } catch {
                        message = "Stream interrompido: \(error.localizedDescription)"
                        ended = true
                    }
                }
                await client.streamStop()
                if Task.isCancelled { break }
                // screenrecord stops itself after the time limit; reopen it
                try? await Task.sleep(nanoseconds: 250_000_000)
            }
            await client.streamStop()
            streamTask = nil
        }
    }

    private func stopLive() {
        streamTask?.cancel()
        streamTask = nil
        Task { await client.streamStop() }
    }

    private func refresh() async {
        guard !loading else { return }
        loading = true
        do {
            let data = try await client.screenshot()
            if let loaded = UIImage(data: data) {
                image = loaded
                message = "\(Int(loaded.size.width))x\(Int(loaded.size.height)) · \(data.count / 1024) KiB"
            } else {
                message = "A TV devolveu \(data.count) bytes que não são um PNG."
            }
        } catch {
            message = "Falhou: \(error.localizedDescription)"
        }
        loading = false
    }
}
