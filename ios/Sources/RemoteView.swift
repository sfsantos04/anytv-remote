import SwiftUI

struct RemoteView: View {
    let device: TVDevice

    @StateObject private var client: TVClient
    @EnvironmentObject private var store: DeviceStore
    @State private var status = "Conectando..."
    @State private var connected = false
    @State private var info = ""
    @State private var typed = ""
    @State private var showApps = false
    @State private var showScreen = false
    @State private var appsAvailable = false

    init(device: TVDevice) {
        self.device = device
        _client = StateObject(wrappedValue: TVClient(proto: device.proto,
                                                     host: device.host,
                                                     token: device.token,
                                                     mac: device.mac))
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                header
                statusBar
                dpad
                controlRow([("VOL-", .volDown), ("MUTE", .mute), ("VOL+", .volUp)])
                controlRow([("MENU", .menu), ("HOME", .home), ("BACK", .back)])
                controlRow([("|<", .prev), ("PLAY", .playPause), (">|", .next)])
                controlRow([("ENTRADA", .input), ("INFO", .info), ("AJUSTES", .settings)])
                powerRow
                if client.supports(Cap.text) { textInput }
                Spacer(minLength: 8)
            }
            .padding()
        }
        .navigationTitle(device.label)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItemGroup(placement: .navigationBarTrailing) {
                if appsAvailable || client.supports(Cap.apps) {
                    Button { showApps = true } label: { Image(systemName: "square.grid.2x2") }
                        .disabled(!connected)
                }
                if client.supports(Cap.screen) {
                    Button { showScreen = true } label: { Image(systemName: "rectangle.on.rectangle") }
                        .disabled(!connected)
                }
            }
        }
        .sheet(isPresented: $showApps) {
            AppsSheet(client: client, canLaunch: client.supports(Cap.launch))
        }
        .sheet(isPresented: $showScreen) { ScreenSheet(client: client) }
        .task { await connect() }
    }

    private var header: some View {
        VStack(spacing: 4) {
            Text(info.isEmpty ? device.proto.title : info)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Text("\(device.host):\(device.port) - \(device.proto.title)")
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
                .lineLimit(4)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
            Button(connected ? "Testar" : "Reconectar") {
                Task { await connect() }
            }
            .font(.footnote)
            .buttonStyle(.bordered)
        }
        .padding(10)
        .background(Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 10))
    }

    private var dpad: some View {
        VStack(spacing: 8) {
            KeyCap(label: "▲") { send(.up) }
            HStack(spacing: 8) {
                KeyCap(label: "◀") { send(.left) }
                KeyCap(label: "OK", prominent: true) { send(.ok) }
                KeyCap(label: "▶") { send(.right) }
            }
            KeyCap(label: "▼") { send(.down) }
        }
        .padding(.vertical, 4)
    }

    private var powerRow: some View {
        HStack(spacing: 8) {
            if client.supports(Cap.powerOn) {
                KeyCap(label: "LIGAR") {
                    perform("Ligando por Wake-on-LAN") { try await client.powerOn() }
                }
            }
            KeyCap(label: "DESLIGAR", tint: .red) {
                perform("Desligando") { try await client.sleep() }
            }
        }
    }

    private var textInput: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Digitar na TV").font(.caption).foregroundStyle(.secondary)
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

    private func controlRow(_ items: [(String, Cmd)]) -> some View {
        HStack(spacing: 8) {
            ForEach(Array(items.enumerated()), id: \.offset) { entry in
                KeyCap(label: entry.element.0) { send(entry.element.1) }
            }
        }
    }

    private func send(_ cmd: Cmd) {
        perform("OK") { try await client.send(cmd) }
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
        status = "Conectando em \(device.host)... \(device.proto.setup)"
        connected = false
        do {
            let result = try await client.connect(host: device.host, port: device.port)
            connected = true
            var label = result.model
            if !result.android.isEmpty { label += " - Android \(result.android)" }
            if label.isEmpty { label = device.proto.title }
            info = label
            status = "Conectado por \(device.proto.title)."
            appsAvailable = await client.probeApps()

            var updated = device
            updated.model = result.model.isEmpty ? device.model : result.model
            updated.android = result.android
            // modelo, resolucao e MAC vem da TV (Tizen: http://host:8001/api/v2/)
            if let details = try? await client.info() {
                let parts = details.split(separator: "|", omittingEmptySubsequences: false)
                if parts.count > 0, !parts[0].isEmpty { updated.model = String(parts[0]) }
                if parts.count > 2, !parts[2].isEmpty { info = "\(parts[0]) - \(parts[2])" }
            }
            let token = client.storedToken()
            if !token.isEmpty {
                updated.token = token
                TokenVault.save(token, host: device.host, proto: device.proto)
                if device.proto == .tizen || device.proto == .webos {
                    status += " Token guardado (não pede Permitir de novo)."
                }
            } else if device.proto == .tizen || device.proto == .webos {
                status += " Sem token salvo: a TV vai pedir Permitir agora."
            }
            let mac = client.discoveredMac()
            if !mac.isEmpty { updated.mac = mac }
            store.update(updated)
        } catch {
            connected = false
            status = "Nao conectou: \(error.localizedDescription)"
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
