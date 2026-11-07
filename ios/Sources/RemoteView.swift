import SwiftUI

struct RemoteView: View {
    let device: TVDevice

    @StateObject private var client = TVClient()
    @State private var status = "Conectando..."
    @State private var connected = false
    @State private var info = ""
    @State private var typed = ""
    @State private var showApps = false
    @State private var showScreen = false

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                header
                statusBar
                dpad
                controlRow([("VOL-", TvKey.volumeDown), ("MUTE", TvKey.mute), ("VOL+", TvKey.volumeUp)])
                controlRow([("MENU", TvKey.menu), ("HOME", TvKey.home), ("BACK", TvKey.back)])
                controlRow([("|<", TvKey.mediaPrevious), ("PLAY", TvKey.playPause), (">|", TvKey.mediaNext)])
                controlRow([("ENTRADA", TvKey.tvInput), ("INFO", TvKey.info), ("AJUSTES", TvKey.settings)])
                powerRow
                textInput
                Spacer(minLength: 8)
            }
            .padding()
        }
        .navigationTitle(device.label)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItemGroup(placement: .navigationBarTrailing) {
                Button { showApps = true } label: { Image(systemName: "square.grid.2x2") }
                    .disabled(!connected)
                Button { showScreen = true } label: { Image(systemName: "rectangle.on.rectangle") }
                    .disabled(!connected)
            }
        }
        .sheet(isPresented: $showApps) { AppsSheet(client: client) }
        .sheet(isPresented: $showScreen) { ScreenSheet(client: client) }
        .task { await connect() }
    }

    private var header: some View {
        VStack(spacing: 4) {
            Text(info.isEmpty ? device.host : info)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Text("\(device.host):\(device.port)")
                .font(.caption2)
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
    }

    private var statusBar: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(connected ? Color.green : Color.orange)
                .frame(width: 8, height: 8)
            Text(status)
                .font(.footnote)
                .lineLimit(3)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
            if !connected {
                Button("Reconectar") {
                    Task { await connect() }
                }
                .font(.footnote)
                .buttonStyle(.bordered)
            }
        }
        .padding(10)
        .background(Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 10))
    }

    private var dpad: some View {
        VStack(spacing: 8) {
            KeyCap(label: "▲") { send(TvKey.dpadUp) }
            HStack(spacing: 8) {
                KeyCap(label: "◀") { send(TvKey.dpadLeft) }
                KeyCap(label: "OK", prominent: true) { send(TvKey.dpadCenter) }
                KeyCap(label: "▶") { send(TvKey.dpadRight) }
            }
            KeyCap(label: "▼") { send(TvKey.dpadDown) }
        }
        .padding(.vertical, 4)
    }

    private var powerRow: some View {
        HStack(spacing: 8) {
            KeyCap(label: "LIGAR TV") { perform("TV acordada") { try await client.wake() } }
            KeyCap(label: "DESLIGAR", tint: .red) {
                perform("Enviado desligar") { try await client.key(TvKey.sleep) }
            }
        }
    }

    private var textInput: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Digitar na TV")
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack {
                TextField("texto para a TV", text: $typed)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .autocapitalization(.none)
                Button("Enviar") {
                    let value = typed
                    typed = ""
                    perform("Texto enviado") { try await client.text(value) }
                }
                .buttonStyle(.borderedProminent)
                .disabled(!connected || typed.isEmpty)
            }
        }
    }

    private func controlRow(_ items: [(String, Int)]) -> some View {
        HStack(spacing: 8) {
            ForEach(Array(items.enumerated()), id: \.offset) { entry in
                KeyCap(label: entry.element.0) { send(entry.element.1) }
            }
        }
    }

    private func send(_ code: Int) {
        perform("Tecla enviada") { try await client.key(code) }
    }

    private func perform(_ success: String, _ operation: @escaping () async throws -> Void) {
        Task {
            do {
                try await operation()
                status = success
            } catch {
                status = "Falhou: \(error.localizedDescription)"
            }
        }
    }

    private func connect() async {
        status = "Conectando em \(device.host)... se a TV mostrar um pedido, aceite na tela."
        connected = false
        do {
            let result = try await client.connect(host: device.host, port: device.port)
            connected = true
            info = "\(result.model.isEmpty ? "Android TV" : result.model) - Android \(result.android)"
            status = "Conectado."
        } catch {
            connected = false
            status = "Nao conectou: \(error.localizedDescription). Confira a depuracao na TV e toque em Reconectar."
        }
    }
}

struct KeyCap: View {
    let label: String
    var prominent: Bool = false
    var tint: Color = .accentColor
    let action: () -> Void
    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(prominent ? .headline : .subheadline)
                .fontWeight(.semibold)
                .frame(minWidth: 56, minHeight: 48)
                .frame(maxWidth: .infinity)
                .background(
                    RoundedRectangle(cornerRadius: 12)
                        .fill(prominent ? tint.opacity(0.35) : Color.secondary.opacity(0.18))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 12)
                        .stroke(prominent ? tint : Color.clear, lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
        .foregroundStyle(.primary)
        .opacity(isEnabled ? 1 : 0.5)
    }
}
