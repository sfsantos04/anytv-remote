import SwiftUI

// Tela cheia da Samsung, no formato do app oficial: fundo claro, power no alto,
// balancins verticais de volume e canal (cada metade é uma tecla que afunda e
// vibra), microfone no centro, roda grande embaixo e as teclas de mídia na base.

private let ink = Color(red: 0.52, green: 0.54, blue: 0.57)
private let inkStrong = Color(red: 0.30, green: 0.32, blue: 0.35)
private let cardFace = Color.white
private let powerRed = Color(red: 0.85, green: 0.20, blue: 0.23)

struct SamsungRemote: View {
    let client: TVClient
    var title: String
    var canApps: Bool
    var canScreen: Bool
    var onClose: () -> Void

    @State private var status = ""
    @State private var showApps = false
    @State private var showScreen = false
    @State private var showDiag = false
    @State private var showNumbers = false
    @State private var showText = false
    @State private var typed = ""

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        status = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width
            VStack(spacing: 0) {
                topBar
                    .padding(.horizontal, 22)
                    .padding(.top, 6)

                HStack(alignment: .center, spacing: 0) {
                    leftRail
                    Spacer(minLength: 0)
                    rocker(.volUp, .volDown, "VOL")
                    Spacer(minLength: 0)
                    centerColumn
                    Spacer(minLength: 0)
                    rocker(.chUp, .chDown, "CH")
                    Spacer(minLength: 0)
                    rightRail
                }
                .padding(.horizontal, 10)
                .padding(.top, 26)

                homeRow
                    .padding(.horizontal, 34)
                    .padding(.top, 30)

                wheel(diameter: min(wide - 44, 330))
                    .padding(.top, 26)

                Spacer(minLength: 0)

                mediaRow
                    .padding(.horizontal, 40)
                    .padding(.bottom, 18)
            }
        }
        .background(
            LinearGradient(colors: [Color(red: 0.95, green: 0.96, blue: 0.97),
                                    Color(red: 0.88, green: 0.90, blue: 0.93)],
                           startPoint: .topLeading, endPoint: .bottomTrailing)
                .ignoresSafeArea()
        )
        .sheet(isPresented: $showApps) {
            AppsSheet(client: client, canLaunch: client.supports(Cap.launch))
        }
        .sheet(isPresented: $showScreen) { ScreenSheet(client: client) }
        .sheet(isPresented: $showDiag) { DiagnosticsView() }
        .sheet(isPresented: $showNumbers) { numberSheet }
        .sheet(isPresented: $showText) { textSheet }
    }

    // MARK: - Barra do alto

    private var topBar: some View {
        HStack {
            Button(action: onClose) {
                Image(systemName: "chevron.down")
                    .font(.system(size: 22, weight: .semibold))
                    .foregroundStyle(inkStrong)
                    .frame(width: 44, height: 44)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Fechar")

            Spacer(minLength: 0)

            Button {
                UIImpactFeedbackGenerator(style: .medium).impactOccurred()
                status = "Power"
                Task { try? await client.send(.power) }
            } label: {
                Image(systemName: "power")
                    .font(.system(size: 26, weight: .regular))
                    .foregroundStyle(powerRed)
                    .frame(width: 56, height: 56)
                    .background(
                        Circle()
                            .strokeBorder(powerRed, lineWidth: 2)
                            .background(Circle().fill(Color.white.opacity(0.75)))
                    )
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Power")

            Spacer(minLength: 0)

            Menu {
                if canApps { Button("Aplicativos") { showApps = true } }
                if canScreen { Button("Espelhar tela") { showScreen = true } }
                Button("Diagnóstico") { showDiag = true }
            } label: {
                Image(systemName: "ellipsis")
                    .font(.system(size: 22, weight: .bold))
                    .rotationEffect(.degrees(90))
                    .foregroundStyle(inkStrong)
                    .frame(width: 44, height: 44)
            }
            .accessibilityLabel("Mais")
        }
    }

    // MARK: - Laterais: mudo e guia

    private var leftRail: some View {
        Button { tap(.mute, "Mudo") } label: {
            VStack(spacing: 4) {
                Image(systemName: "speaker.slash.fill")
                    .font(.system(size: 20))
                Text("Mudo").font(.system(size: 10))
            }
            .foregroundStyle(ink)
            .frame(width: 46)
        }
        .buttonStyle(SoftPress())
        .accessibilityLabel("Mudo")
    }

    private var rightRail: some View {
        Button { tap(.guide, "Guia") } label: {
            VStack(spacing: 4) {
                Image(systemName: "rectangle.grid.1x2")
                    .font(.system(size: 20))
                Text("Guia").font(.system(size: 10))
            }
            .foregroundStyle(ink)
            .frame(width: 46)
        }
        .buttonStyle(SoftPress())
        .accessibilityLabel("Guia")
    }

    // MARK: - Balancim vertical (volume e canal)

    /// Pílula vertical: metade de cima aumenta, metade de baixo diminui.
    private func rocker(_ up: Cmd, _ down: Cmd, _ label: String) -> some View {
        VStack(spacing: 0) {
            Button { tap(up, "\(label) +") } label: {
                Image(systemName: "plus")
                    .font(.system(size: 22, weight: .light))
                    .foregroundStyle(ink)
                    .frame(width: 66, height: 62)
                    .contentShape(Rectangle())
            }
            .buttonStyle(SoftPress())

            Text(label)
                .font(.system(size: 15, weight: .regular))
                .foregroundStyle(ink.opacity(0.75))
                .frame(width: 66, height: 40)

            Button { tap(down, "\(label) -") } label: {
                Image(systemName: "minus")
                    .font(.system(size: 22, weight: .light))
                    .foregroundStyle(ink)
                    .frame(width: 66, height: 62)
                    .contentShape(Rectangle())
            }
            .buttonStyle(SoftPress())
        }
        .background(
            Capsule()
                .fill(cardFace)
                .overlay(Capsule().stroke(Color.white.opacity(0.9), lineWidth: 1))
                .shadow(color: .black.opacity(0.10), radius: 10, y: 4)
                .shadow(color: .white.opacity(0.9), radius: 1, y: -1)
        )
    }

    // MARK: - Microfone e bolinhas

    private var centerColumn: some View {
        VStack(spacing: 10) {
            Button {
                // o microfone do teclado do iPhone faz o ditado; o texto vai
                // para a TV pelo canal de texto do protocolo
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
                typed = ""
                showText = true
            } label: {
                Image(systemName: "mic")
                    .font(.system(size: 26, weight: .light))
                    .foregroundStyle(ink)
                    .frame(width: 52, height: 52)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Microfone")

            HStack(spacing: 3) {
                Circle().fill(Color(red: 0.86, green: 0.26, blue: 0.32)).frame(width: 5, height: 5)
                Circle().fill(Color(red: 0.35, green: 0.68, blue: 0.36)).frame(width: 5, height: 5)
                Circle().fill(Color(red: 0.94, green: 0.80, blue: 0.24)).frame(width: 5, height: 5)
                Circle().fill(Color(red: 0.30, green: 0.55, blue: 0.86)).frame(width: 5, height: 5)
            }
        }
        .frame(width: 70)
    }

    // MARK: - Voltar, home e 123

    private var homeRow: some View {
        HStack {
            Button { tap(.back, "Voltar") } label: {
                Image(systemName: "arrow.uturn.left")
                    .font(.system(size: 24, weight: .light))
                    .foregroundStyle(ink)
                    .frame(width: 56, height: 44)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Voltar")

            Spacer(minLength: 0)

            Button { tap(.home, "Home") } label: {
                Image(systemName: "house")
                    .font(.system(size: 24, weight: .light))
                    .foregroundStyle(ink)
                    .frame(width: 56, height: 44)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Home")

            Spacer(minLength: 0)

            Button {
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
                showNumbers = true
            } label: {
                HStack(spacing: 0) {
                    Circle()
                        .fill(Color.white)
                        .shadow(color: .black.opacity(0.12), radius: 2, y: 1)
                        .frame(width: 30, height: 30)
                        .padding(.leading, 3)
                    Spacer(minLength: 0)
                    Text("123")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(ink)
                        .padding(.trailing, 12)
                }
                .frame(width: 96, height: 34)
                .background(Capsule().fill(Color(red: 0.90, green: 0.91, blue: 0.93)))
            }
            .accessibilityLabel("Teclado numerico")
        }
    }

    // MARK: - Roda grande

    private func wheel(diameter: CGFloat) -> some View {
        let center = diameter * 0.42
        return ZStack {
            Circle()
                .fill(LinearGradient(colors: [Color.white, Color(red: 0.92, green: 0.93, blue: 0.95)],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Circle().stroke(Color.white.opacity(0.95), lineWidth: 1))
                .shadow(color: .black.opacity(0.08), radius: 14, y: 6)
                .frame(width: diameter, height: diameter)

            Button { tap(.ok, "OK") } label: {
                Circle()
                    .fill(Color.white)
                    .overlay(Circle().stroke(Color(red: 0.90, green: 0.91, blue: 0.93), lineWidth: 1))
                    .frame(width: center, height: center)
                    .shadow(color: .black.opacity(0.06), radius: 6, y: 3)
            }
            .buttonStyle(SoftPress(scale: 0.96))
            .accessibilityLabel("OK")

            VStack {
                chevron(.up, "Cima")
                Spacer(minLength: 0)
                chevron(.down, "Baixo")
            }
            .frame(width: diameter, height: diameter)
            .padding(.vertical, 14)

            HStack {
                chevron(.left, "Esquerda")
                Spacer(minLength: 0)
                chevron(.right, "Direita")
            }
            .frame(width: diameter, height: diameter)
            .padding(.horizontal, 14)
        }
        .frame(height: diameter)
    }

    private func chevron(_ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: "chevron.\(label == "Cima" ? "up" : label == "Baixo" ? "down" : label == "Esquerda" ? "left" : "right")")
                .font(.system(size: 22, weight: .light))
                .foregroundStyle(ink.opacity(0.55))
                .frame(width: 52, height: 46)
                .contentShape(Rectangle())
        }
        .buttonStyle(SoftPress(scale: 0.9))
        .accessibilityLabel(label)
    }

    // MARK: - Mídia

    private var mediaRow: some View {
        HStack {
            Button { tap(.prev, "Anterior") } label: {
                Image(systemName: "backward.end.fill")
                    .font(.system(size: 22))
                    .foregroundStyle(inkStrong)
                    .frame(width: 60, height: 50)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Anterior")

            Spacer(minLength: 0)

            Button { tap(.playPause, "Play/Pause") } label: {
                Image(systemName: "playpause.fill")
                    .font(.system(size: 24))
                    .foregroundStyle(inkStrong)
                    .frame(width: 60, height: 50)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Play/Pause")

            Spacer(minLength: 0)

            Button { tap(.next, "Próximo") } label: {
                Image(systemName: "forward.end.fill")
                    .font(.system(size: 22))
                    .foregroundStyle(inkStrong)
                    .frame(width: 60, height: 50)
            }
            .buttonStyle(SoftPress())
            .accessibilityLabel("Próximo")
        }
        .overlay(alignment: .top) {
            Rectangle()
                .fill(Color.black.opacity(0.06))
                .frame(height: 1)
                .padding(.horizontal, -8)
                .offset(y: -10)
        }
    }

    // MARK: - Digitar / ditar

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
            Text("Toque no microfone do teclado para ditar em vez de digitar.")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer(minLength: 0)
        }
        .padding(20)
        .presentationDetents([.height(260)])
    }

    // MARK: - Teclado numérico

    private var numberSheet: some View {
        VStack(spacing: 14) {
            HStack {
                Text("Teclado").font(.headline)
                Spacer()
                Button("Fechar") { showNumbers = false }
            }
            .padding(.horizontal, 4)

            ForEach([[1, 2, 3], [4, 5, 6], [7, 8, 9]], id: \.self) { line in
                HStack(spacing: 12) {
                    ForEach(line, id: \.self) { digit in
                        numberButton(digit)
                    }
                }
            }
            HStack(spacing: 12) {
                labelledButton("CH-") { tap(.chDown, "CH-") }
                numberButton(0)
                labelledButton("CH+") { tap(.chUp, "CH+") }
            }
            Spacer(minLength: 0)
        }
        .padding(20)
        .presentationDetents([.medium])
    }

    private func numberButton(_ digit: Int) -> some View {
        let cmd = Cmd(rawValue: Cmd.num0.rawValue + Int32(digit)) ?? .num0
        return Button { tap(cmd, "\(digit)") } label: {
            Text("\(digit)")
                .font(.system(size: 24, weight: .regular))
                .foregroundStyle(inkStrong)
                .frame(maxWidth: .infinity)
                .frame(height: 54)
                .background(RoundedRectangle(cornerRadius: 12).fill(Color.white)
                    .shadow(color: .black.opacity(0.08), radius: 4, y: 2))
        }
        .buttonStyle(SoftPress())
        .accessibilityLabel("Número \(digit)")
    }

    private func labelledButton(_ label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(ink)
                .frame(maxWidth: .infinity)
                .frame(height: 54)
                .background(RoundedRectangle(cornerRadius: 12).fill(Color(red: 0.92, green: 0.93, blue: 0.95)))
        }
        .buttonStyle(SoftPress())
    }
}

/// Tecla clara que afunda e encolhe levemente ao toque.
struct SoftPress: ButtonStyle {
    var scale: CGFloat = 0.93

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? scale : 1)
            .opacity(configuration.isPressed ? 0.65 : 1)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}
