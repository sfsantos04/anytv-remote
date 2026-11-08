import SwiftUI

// Tela cheia do controle: a imagem renderizada como fundo e as áreas de toque
// por cima. Os botões de fechar e de menu ficam flutuando nos cantos, porque a
// imagem é idêntica ao controle físico.

struct PhotoRemoteScreen: View {
    let client: TVClient
    let proto: TVProto
    var canApps: Bool
    var canScreen: Bool
    var connected: Bool
    var status: String
    var onRetry: () -> Void
    var onClose: () -> Void

    @State private var status = ""
    @State private var showApps = false
    @State private var showScreen = false
    @State private var showDiag = false
    @State private var showNumbers = false
    @State private var showExtra = false
    @State private var showText = false
    @State private var typed = ""

    @State private var wantKeyboard = false

    private var art: (name: String, size: CGSize, keys: [HotKey]) {
        proto == .tizen ? ("samsung.jpg", RemoteArt.base, RemoteArt.samsung)
                        : ("android.jpg", RemoteArt.androidBase, RemoteArt.android)
    }

    var body: some View {
        Group {
            if RemoteArt.has(art.name) {
                photoRemote
            } else {
                // rede de seguranca: se a imagem faltar no pacote, o controle
                // desenhado em codigo assume (nunca fica em branco)
                vectorRemote
            }
        }
        .background(Color.black)
    }

    @ViewBuilder
    private var vectorRemote: some View {
        if proto == .tizen {
            SamsungRemote(client: client, title: "Samsung",
                          canApps: canApps, canScreen: canScreen,
                          onClose: onClose)
        } else {
            AndroidPad(client: client, title: "Android", wantKeyboard: $wantKeyboard)
        }
    }

    private var photoRemote: some View {
        ZStack(alignment: .topLeading) {
            PhotoRemote(imageName: art.name,
                        imageSize: art.size,
                        keys: art.keys,
                        special: handleSpecial) { cmd, label in
                status = label
                Task { try? await client.send(cmd) }
            }

            // fechar e menu, discretos, sobre a imagem
            HStack {
                Button(action: onClose) {
                    Image(systemName: "chevron.down")
                        .font(.system(size: 20, weight: .semibold))
                        .foregroundStyle(.white.opacity(0.75))
                        .frame(width: 44, height: 44)
                        .background(Circle().fill(Color.black.opacity(0.28)))
                }
                .accessibilityLabel("Fechar")

                Spacer(minLength: 0)

                // indicador de conexão: verde conectado, laranja conectando,
                // vermelho sem conexão. Tocar tenta de novo.
                Button { onRetry() } label: {
                    HStack(spacing: 5) {
                        Circle()
                            .fill(connected ? Color.green : (status.lowercased().contains("conect") ? Color.orange : Color.red))
                            .frame(width: 7, height: 7)
                        Text(connected ? "Conectado" : (status.isEmpty ? "Sem conexão" : status))
                            .font(.system(size: 11, weight: .medium))
                            .foregroundStyle(.white.opacity(0.85))
                            .lineLimit(1)
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 6)
                    .background(Capsule().fill(Color.black.opacity(0.32)))
                }
                .accessibilityLabel("Estado da conexão")

                Spacer(minLength: 0)

                Menu {
                    if canApps { Button("Aplicativos") { showApps = true } }
                    if canScreen { Button("Espelhar tela") { showScreen = true } }
                    if proto == .tizen || proto == .adb {
                        Button("Teclas extras") { showExtra = true }
                    }
                    Button("Digitar na TV") { typed = ""; showText = true }
                    Button("Diagnóstico") { showDiag = true }
                } label: {
                    Image(systemName: "ellipsis")
                        .font(.system(size: 20, weight: .bold))
                        .rotationEffect(.degrees(90))
                        .foregroundStyle(.white.opacity(0.75))
                        .frame(width: 44, height: 44)
                        .background(Circle().fill(Color.black.opacity(0.28)))
                }
                .accessibilityLabel("Mais")
            }
            .padding(.horizontal, 14)
            .padding(.top, 8)
        }
        .overlay {
            if !connected {
                VStack(spacing: 10) {
                    Text("Sem conexão").font(.headline)
                    Text(status.isEmpty ? "Tocando em Conectar o app tenta de novo." : status)
                        .font(.caption)
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                    Button("Conectar") { onRetry() }
                        .buttonStyle(.borderedProminent)
                }
                .padding(20)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 18))
                .padding(.horizontal, 40)
            }
        }
        .statusBarHidden(false)
        .sheet(isPresented: $showApps) {
            AppsSheet(client: client, canLaunch: client.supports(Cap.launch))
        }
        .sheet(isPresented: $showScreen) { ScreenSheet(client: client) }
        .sheet(isPresented: $showDiag) { DiagnosticsView() }
        .sheet(isPresented: $showNumbers) { numberSheet }
        .sheet(isPresented: $showExtra) { extraSheet }
        .sheet(isPresented: $showText) { textSheet }
    }

    // MARK: - Teclas que não são comando direto

    private func handleSpecial(_ id: String) {
        switch id {
        case "123": showNumbers = true
        case "extra": showExtra = true
        case "teclado": typed = ""; showText = true
        case "netflix": launch("netflix", "Netflix")
        case "globoplay": launch("globoplay", "Globoplay")
        default: break
        }
    }

    private func launch(_ needle: String, _ label: String) {
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        status = label
        Task {
            guard let apps = try? await client.apps() else { return }
            let target = apps.first { $0.name.lowercased().contains(needle) }
                ?? apps.first { $0.id.lowercased().contains(needle) }
            if let target { try? await client.launch(target.id) }
        }
    }

    // MARK: - Folhas

    private var numberSheet: some View {
        VStack(spacing: 14) {
            HStack {
                Text("Teclado").font(.headline)
                Spacer()
                Button("Fechar") { showNumbers = false }
            }
            ForEach([[1, 2, 3], [4, 5, 6], [7, 8, 9]], id: \.self) { line in
                HStack(spacing: 12) {
                    ForEach(line, id: \.self) { d in
                        digitButton(d)
                    }
                }
            }
            HStack(spacing: 12) {
                sideButton("CH-") { send(.chDown) }
                digitButton(0)
                sideButton("CH+") { send(.chUp) }
            }
            Spacer(minLength: 0)
        }
        .padding(20)
        .presentationDetents([.medium])
    }

    private func digitButton(_ d: Int) -> some View {
        let cmd = Cmd(rawValue: Cmd.num0.rawValue + Int32(d)) ?? .num0
        return Button { send(cmd) } label: {
            Text("\(d)")
                .font(.system(size: 24))
                .foregroundStyle(.primary)
                .frame(maxWidth: .infinity)
                .frame(height: 56)
                .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
        }
        .accessibilityLabel("Número \(d)")
    }

    private func sideButton(_ label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity)
                .frame(height: 56)
                .background(RoundedRectangle(cornerRadius: 12).fill(Color(.tertiarySystemBackground)))
        }
    }

    private var extraSheet: some View {
        VStack(spacing: 18) {
            HStack {
                Text("Teclas extras").font(.headline)
                Spacer()
                Button("Fechar") { showExtra = false }
            }
            HStack(spacing: 18) {
                colorDot(Color(red: 0.80, green: 0.19, blue: 0.20), .red, "Vermelho")
                colorDot(Color(red: 0.28, green: 0.62, blue: 0.30), .green, "Verde")
                colorDot(Color(red: 0.88, green: 0.74, blue: 0.20), .yellow, "Amarelo")
                colorDot(Color(red: 0.20, green: 0.42, blue: 0.80), .blue, "Azul")
            }
            HStack(spacing: 12) {
                wideButton("Fonte") { send(.input) }
                wideButton("Mudo") { send(.mute) }
                wideButton("Info") { send(.info) }
                wideButton("Guia") { send(.guide) }
            }
            Spacer(minLength: 0)
        }
        .padding(20)
        .presentationDetents([.height(280)])
    }

    private func colorDot(_ color: Color, _ cmd: Cmd, _ label: String) -> some View {
        Button { send(cmd) } label: {
            Circle().fill(color)
                .frame(width: 44, height: 44)
                .shadow(color: color.opacity(0.4), radius: 4, y: 2)
        }
        .accessibilityLabel(label)
    }

    private func wideButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 14, weight: .medium))
                .frame(maxWidth: .infinity)
                .frame(height: 46)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color(.secondarySystemBackground)))
        }
    }

    private var textSheet: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Digitar ou ditar").font(.headline)
                Spacer()
                Button("Fechar") { showText = false }
            }
            TextField("texto para a TV", text: $typed)
                .textFieldStyle(.roundedBorder)
                .autocorrectionDisabled()
                .autocapitalization(.none)
            HStack {
                Spacer()
                Button("Enviar para a TV") {
                    let value = typed
                    typed = ""
                    showText = false
                    Task { try? await client.text(value) }
                }
                .buttonStyle(.borderedProminent)
                .disabled(typed.isEmpty)
            }
            Text("Toque no microfone do teclado para ditar.")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
        }
        .padding(20)
        .presentationDetents([.height(260)])
    }

    private func send(_ cmd: Cmd) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        status = ""
        Task { try? await client.send(cmd) }
    }
}
