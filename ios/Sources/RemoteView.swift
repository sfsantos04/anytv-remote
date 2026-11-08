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
    @State private var showDiag = false
    @State private var appsAvailable = false
    @State private var showKeyboard = false
    @FocusState private var typing: Bool
    @Environment(\.dismiss) private var dismiss

    init(device: TVDevice) {
        self.device = device
        _client = StateObject(wrappedValue: TVClientPool.shared.client(for: device))
    }

    var body: some View {
        content
        .onChange(of: showKeyboard) { _ in typing = true }
        .navigationTitle(device.label)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar((device.proto == .tizen || device.proto == .adb) ? .hidden : .visible, for: .navigationBar)
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
                Menu {
                    Button("Diagnóstico") { showDiag = true }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
            }
        }
        .sheet(isPresented: $showApps) {
            AppsSheet(client: client, canLaunch: client.supports(Cap.launch))
        }
        .sheet(isPresented: $showScreen) { ScreenSheet(client: client) }
        .sheet(isPresented: $showDiag) { DiagnosticsView() }
        .task {
            // cliente reaproveitado: se a sessão da TV já está de pé, não reconecta
            if client.isConnected {
                connected = true
                status = "Conectado por \(device.proto.title)."
            } else {
                await connect()
            }
        }
        // A sessao NAO e fechada ao sair da tela: medido que cada conexao nova
        // sem token cria um cliente novo na TV e faz ela pedir "Permitir".
        // Um unico cliente vivo por aparelho (o pool) mantem a autorizacao.
    }


    /// Samsung usa a tela cheia clara; os outros protocolos seguem na lista.
    @ViewBuilder
    private var content: some View {
        if device.proto == .tizen || device.proto == .adb {
            PhotoRemoteScreen(client: client,
                              proto: device.proto,
                              canApps: appsAvailable || client.supports(Cap.apps),
                              canScreen: client.supports(Cap.screen),
                              connected: connected,
                              connectionStatus: status,
                              onRetry: { Task { await connect() } },
                              onClose: { dismiss() })
        } else {
        ScrollView {
            VStack(spacing: 14) {
                header
                statusBar
                if false {
                } else {
                    dpad
                    controlRow([("VOL-", .volDown), ("MUTE", .mute), ("VOL+", .volUp)])
                    controlRow([("MENU", .menu), ("HOME", .home), ("BACK", .back)])
                    controlRow([("|<", .prev), ("PLAY", .playPause), (">|", .next)])
                    controlRow([("ENTRADA", .input), ("INFO", .info), ("AJUSTES", .settings)])
                    powerRow
                }
                if client.supports(Cap.text) { textInput }
                Spacer(minLength: 8)
            }
            .padding()
        }
        }
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
                Task {
                    if connected {
                        // teste leve: mesma leitura que o Android faz no connect,
                        // sem abrir sessão nova (sessão extra = "Permitir" de novo)
                        if let details = try? await client.info(), !details.isEmpty {
                            info = details.replacingOccurrences(of: "|", with: " - ")
                            status = "Testado: \(info)"
                        } else {
                            status = "A TV não respondeu ao teste."
                            connected = false
                        }
                    } else {
                        await connect()
                    }
                }
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
                    .focused($typing)
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
        // nunca abre uma segunda sessão na mesma TV: sessão extra = Permitir de novo
        if client.isConnected {
            connected = true
            status = "Conectado por \(device.proto.title)."
            return
        }
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
            appsAvailable = client.supports(Cap.apps)

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
            if device.proto == .adb {
                // teclas rápidas: sobe o canal de controle do scrcpy em segundo
                // plano; se não subir, o envio continua pelo ADB normal
                Task { await client.startFastKeys() }
                if let probe = try? await client.shell("id; which su; ls /dev/input 2>/dev/null | head -8") {
                    Diagnostics.note("ADB probe: " + probe.replacingOccurrences(of: "\n", with: " | "))
                }
            }
            let mac = client.discoveredMac()
            if !mac.isEmpty { updated.mac = mac }
            store.update(updated)
        } catch {
            connected = false
            let raw = error.localizedDescription.lowercased()
            if raw.contains("handshake") || raw.contains("read failed") {
                status = device.proto == .adb
                    ? "O ADB do aparelho nao respondeu. Desligue e ligue a Depuracao USB, ou reinicie o aparelho."
                    : "A TV nao completou o handshake. Confira se ela esta ligada e na mesma rede."
            } else if raw.contains("caiu") || raw.contains("nao conectado") {
                status = "Conexao caiu. Toque em Conectar para tentar de novo."
            } else {
                status = "Nao conectou: \(error.localizedDescription)"
            }
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
