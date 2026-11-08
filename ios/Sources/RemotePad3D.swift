import SwiftUI

// One Remote da Samsung (MU6100), fiel à foto: ranhura no alto, power à
// esquerda, 123 e EXTRA na linha seguinte, roda lisa com o OK no centro, três
// teclas redondas (voltar, home, play/pause) e a barra única VOL | CH.

private let bodyLight = Color(red: 0.26, green: 0.26, blue: 0.27)
private let bodyDark = Color(red: 0.15, green: 0.15, blue: 0.16)
private let keyFace = Color(red: 0.22, green: 0.22, blue: 0.23)
private let keyEdge = Color(red: 0.12, green: 0.12, blue: 0.13)

// usados pelo estilo antigo, mantido para as outras telas
private let keyTop = Color(red: 0.30, green: 0.32, blue: 0.35)
private let keyBottom = Color(red: 0.15, green: 0.16, blue: 0.18)

/// Estilo com profundidade (usado fora do controle da Samsung).
struct BevelButtonStyle: ButtonStyle {
    var shape: AnyShape = AnyShape(Circle())
    var glow: Color = .clear

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                shape
                    .fill(LinearGradient(colors: [keyTop, keyBottom],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(
                        shape.stroke(LinearGradient(colors: [Color.white.opacity(0.35),
                                                             Color.black.opacity(0.45)],
                                                    startPoint: .top, endPoint: .bottom),
                                     lineWidth: 1)
                    )
                    .overlay(
                        shape.stroke(glow.opacity(configuration.isPressed ? 0.9 : 0.45),
                                     lineWidth: configuration.isPressed ? 2 : 1)
                    )
            )
            .scaleEffect(configuration.isPressed ? 0.93 : 1)
            .brightness(configuration.isPressed ? 0.06 : 0)
            .shadow(color: .black.opacity(configuration.isPressed ? 0.35 : 0.65),
                    radius: configuration.isPressed ? 1 : 4,
                    x: 0, y: configuration.isPressed ? 1 : 3)
            .animation(.easeOut(duration: 0.09), value: configuration.isPressed)
    }
}

/// Tecla circular preta, com aro fino e leve relevo.
struct RoundKey: ButtonStyle {
    var diameter: CGFloat = 64
    var glow: Color = .clear

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .frame(width: diameter, height: diameter)
            .background(
                Circle()
                    .fill(LinearGradient(colors: [keyFace, keyEdge],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(
                        Circle().strokeBorder(
                            LinearGradient(colors: [Color.white.opacity(0.18),
                                                    Color.black.opacity(0.5)],
                                           startPoint: .top, endPoint: .bottom),
                            lineWidth: 1)
                    )
                    .overlay(
                        Circle().strokeBorder(glow.opacity(configuration.isPressed ? 0.9 : 0.4),
                                              lineWidth: configuration.isPressed ? 2 : 1)
                    )
            )
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .shadow(color: .black.opacity(configuration.isPressed ? 0.3 : 0.55),
                    radius: configuration.isPressed ? 1 : 4, y: configuration.isPressed ? 1 : 3)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}

struct RemotePad3D: View {
    let client: TVClient
    var title: String

    @State private var lastSent = ""
    @State private var panel: Panel = .none
    @State private var showInfo = false
    @State private var details: [(String, String)] = []
    @State private var loadingDetails = false

    enum Panel { case none, numbers, extra }

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        lastSent = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 0) {
            slot
            powerRow
            topKeys
            wheel
            roundRow
            rockerBar
            rockerLabels
            panelArea
            Text(lastSent.isEmpty ? " " : lastSent)
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.30))
                .frame(height: 12)
        }
        .padding(.horizontal, 26)
        .padding(.top, 14)
        .padding(.bottom, 20)
        .frame(maxWidth: 330)
        .background(shellBackground)
        .sheet(isPresented: $showInfo) { infoSheet }
    }

    // MARK: - Corpo

    private var shellBackground: some View {
        RoundedRectangle(cornerRadius: 46, style: .continuous)
            .fill(LinearGradient(colors: [bodyLight, bodyDark],
                                 startPoint: .topLeading, endPoint: .bottomTrailing))
            .overlay(
                RoundedRectangle(cornerRadius: 46, style: .continuous)
                    .strokeBorder(
                        LinearGradient(colors: [Color.white.opacity(0.22),
                                                Color.black.opacity(0.45)],
                                       startPoint: .top, endPoint: .bottom),
                        lineWidth: 1.2)
            )
            .shadow(color: .black.opacity(0.55), radius: 24, x: 0, y: 16)
    }

    /// Ranhura do alto.
    private var slot: some View {
        Capsule()
            .fill(Color.black.opacity(0.55))
            .overlay(Capsule().stroke(Color.white.opacity(0.06), lineWidth: 1))
            .frame(width: 170, height: 9)
            .padding(.top, 2)
            .padding(.bottom, 20)
    }

    private var powerRow: some View {
        HStack {
            Button { tap(.power, "Power") } label: {
                Image(systemName: "power")
                    .font(.system(size: 20, weight: .regular))
                    .foregroundStyle(Color(red: 0.92, green: 0.38, blue: 0.36))
            }
            .buttonStyle(RoundKey(diameter: 66, glow: Color.red.opacity(0.35)))
            .accessibilityLabel("Power")

            Spacer(minLength: 0)
        }
        .padding(.bottom, 16)
    }

    /// 123 à esquerda e EXTRA à direita.
    private var topKeys: some View {
        HStack(alignment: .center) {
            Button {
                withAnimation(.easeInOut(duration: 0.15)) {
                    panel = (panel == .numbers) ? .none : .numbers
                }
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
            } label: {
                Text("123")
                    .font(.system(size: 19, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white.opacity(0.95))
            }
            .buttonStyle(RoundKey(diameter: 78,
                                  glow: panel == .numbers ? Color.white.opacity(0.5) : .clear))
            .accessibilityLabel("Teclado numerico")

            Spacer(minLength: 0)

            Button {
                withAnimation(.easeInOut(duration: 0.15)) {
                    panel = (panel == .extra) ? .none : .extra
                }
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
            } label: {
                VStack(spacing: 3) {
                    HStack(spacing: 2.5) {
                        Circle().fill(Color(red: 0.86, green: 0.26, blue: 0.32)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.35, green: 0.68, blue: 0.36)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.94, green: 0.80, blue: 0.24)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.30, green: 0.55, blue: 0.86)).frame(width: 5, height: 5)
                    }
                    Text("EXTRA")
                        .font(.system(size: 11, weight: .bold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.95))
                }
            }
            .buttonStyle(RoundKey(diameter: 92,
                                  glow: panel == .extra ? Color.white.opacity(0.5) : .clear))
            .accessibilityLabel("Menu EXTRA")
        }
        .padding(.horizontal, 2)
        .padding(.bottom, 18)
    }

    // MARK: - Roda lisa (sem setas, como no controle)

    private var wheel: some View {
        ZStack {
            Circle()
                .fill(LinearGradient(colors: [keyFace, keyEdge],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Circle().strokeBorder(
                    LinearGradient(colors: [Color.white.opacity(0.16),
                                            Color.black.opacity(0.5)],
                                   startPoint: .top, endPoint: .bottom), lineWidth: 1.2))
                .shadow(color: .black.opacity(0.5), radius: 8, y: 5)
                .frame(width: 252, height: 252)

            Circle()
                .strokeBorder(Color.black.opacity(0.30), lineWidth: 16)
                .frame(width: 206, height: 206)

            Button { tap(.ok, "OK") } label: {
                Circle()
                    .fill(LinearGradient(colors: [Color(red: 0.24, green: 0.24, blue: 0.25),
                                                  Color(red: 0.14, green: 0.14, blue: 0.15)],
                                         startPoint: .topLeading, endPoint: .bottomTrailing))
                    .overlay(Circle().strokeBorder(Color.white.opacity(0.14), lineWidth: 1))
                    .frame(width: 126, height: 126)
                    .shadow(color: .black.opacity(0.45), radius: 4, y: 3)
            }
            .buttonStyle(PlainButtonStyle3D())
            .accessibilityLabel("OK")

            VStack {
                arrowZone(.up, "Cima")
                Spacer(minLength: 0)
                arrowZone(.down, "Baixo")
            }
            .frame(width: 252, height: 252)
            .padding(.vertical, 4)

            HStack {
                arrowZone(.left, "Esquerda")
                Spacer(minLength: 0)
                arrowZone(.right, "Direita")
            }
            .frame(width: 252, height: 252)
            .padding(.horizontal, 4)
        }
        .frame(height: 252)
        .padding(.bottom, 18)
    }

    /// A roda não tem setas: as áreas de toque ficam invisíveis no anel.
    private func arrowZone(_ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Color.white.opacity(0.001)
                .frame(width: 74, height: 58)
        }
        .buttonStyle(PlainButtonStyle3D())
        .accessibilityLabel(label)
    }

    // MARK: - Voltar, home, play/pause

    private var roundRow: some View {
        HStack(spacing: 20) {
            roundKey("arrow.uturn.backward", .back, "Voltar")
            roundKey("house", .home, "Home")
            roundKey("playpause", .playPause, "Play/Pause")
        }
        .padding(.bottom, 16)
    }

    private func roundKey(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 19, weight: .regular))
                .foregroundStyle(.white.opacity(0.92))
        }
        .buttonStyle(RoundKey(diameter: 74))
        .accessibilityLabel(label)
    }

    // MARK: - Barra única VOL | CH

    private var rockerBar: some View {
        HStack(spacing: 0) {
            rockerHalf(up: .volUp, down: .volDown, label: "VOL")
            Rectangle()
                .fill(Color.black.opacity(0.55))
                .frame(width: 2, height: 24)
            rockerHalf(up: .chUp, down: .chDown, label: "CH")
        }
        .frame(height: 34)
        .background(
            Capsule()
                .fill(LinearGradient(colors: [keyFace, keyEdge],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Capsule().strokeBorder(
                    LinearGradient(colors: [Color.white.opacity(0.16),
                                            Color.black.opacity(0.5)],
                                   startPoint: .top, endPoint: .bottom), lineWidth: 1))
                .shadow(color: .black.opacity(0.45), radius: 3, y: 2)
        )
    }

    /// Cada metade é um balancim: em cima aumenta, embaixo diminui.
    private func rockerHalf(up: Cmd, down: Cmd, label: String) -> some View {
        VStack(spacing: 0) {
            Button { tap(up, "\(label) +") } label: {
                Color.white.opacity(0.001)
            }
            .buttonStyle(PlainButtonStyle3D())
            .accessibilityLabel("\(label) mais")

            Button { tap(down, "\(label) -") } label: {
                Color.white.opacity(0.001)
            }
            .buttonStyle(PlainButtonStyle3D())
            .accessibilityLabel("\(label) menos")
        }
    }

    private var rockerLabels: some View {
        HStack {
            Text("VOL").frame(maxWidth: .infinity)
            Text("CH").frame(maxWidth: .infinity)
        }
        .font(.system(size: 12, weight: .medium, design: .rounded))
        .foregroundStyle(.white.opacity(0.55))
        .padding(.top, 7)
        .padding(.bottom, 18)
    }

    // MARK: - Painéis na área vazia da base

    @ViewBuilder
    private var panelArea: some View {
        switch panel {
        case .none:
            Spacer(minLength: 84)      // a base do controle original é vazia
        case .numbers:
            numberPad
        case .extra:
            extraPanel
        }
    }

    private var numberPad: some View {
        VStack(spacing: 8) {
            ForEach([[1, 2, 3], [4, 5, 6], [7, 8, 9]], id: \.self) { line in
                HStack(spacing: 14) {
                    ForEach(line, id: \.self) { digit in numberKey(digit) }
                }
            }
            HStack(spacing: 14) {
                padKey("CH-") { tap(.chDown, "CH-") }
                numberKey(0)
                padKey("CH+") { tap(.chUp, "CH+") }
            }
        }
        .padding(.vertical, 4)
    }

    private func numberKey(_ digit: Int) -> some View {
        let cmd = Cmd(rawValue: Cmd.num0.rawValue + Int32(digit)) ?? .num0
        return Button { tap(cmd, "\(digit)") } label: {
            Text("\(digit)")
                .font(.system(size: 18, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.95))
                .frame(width: 62, height: 40)
        }
        .buttonStyle(PadKey())
        .accessibilityLabel("Numero \(digit)")
    }

    private func padKey(_ label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.8))
                .frame(width: 62, height: 40)
        }
        .buttonStyle(PadKey())
    }

    /// EXTRA reúne as teclas coloridas e as funções que não estão no corpo.
    private var extraPanel: some View {
        VStack(spacing: 10) {
            HStack(spacing: 16) {
                colorKey(Color(red: 0.80, green: 0.19, blue: 0.20), .red, "Vermelho")
                colorKey(Color(red: 0.28, green: 0.62, blue: 0.30), .green, "Verde")
                colorKey(Color(red: 0.88, green: 0.74, blue: 0.20), .yellow, "Amarelo")
                colorKey(Color(red: 0.20, green: 0.42, blue: 0.80), .blue, "Azul")
            }

            HStack(spacing: 8) {
                extraKey("Fonte", .input, "Fonte")
                extraKey("Mudo", .mute, "Mudo")
                extraKey("Info", .info, "Info na TV")
                infoKey
            }
        }
        .padding(.vertical, 6)
    }

    private func colorKey(_ color: Color, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Circle()
                .fill(LinearGradient(colors: [color.opacity(0.95), color.opacity(0.55)],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Circle().strokeBorder(Color.black.opacity(0.45), lineWidth: 1))
                .frame(width: 34, height: 34)
                .shadow(color: color.opacity(0.45), radius: 4, y: 2)
        }
        .buttonStyle(PlainButtonStyle3D())
        .accessibilityLabel(label)
    }

    private func extraKey(_ title: String, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Text(title)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.85))
                .frame(width: 60, height: 36)
        }
        .buttonStyle(PadKey())
    }

    /// Painel com o que a TV informa de verdade (modelo, resolução, rede).
    private var infoKey: some View {
        Button {
            details = []
            loadingDetails = true
            showInfo = true
            Task {
                details = (try? await client.details()) ?? []
                loadingDetails = false
            }
        } label: {
            Image(systemName: "info.circle")
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(.white.opacity(0.85))
                .frame(width: 60, height: 36)
        }
        .buttonStyle(PadKey())
        .accessibilityLabel("Informacoes da TV")
    }

    private var infoSheet: some View {
        NavigationStack {
            List {
                if loadingDetails {
                    HStack { ProgressView(); Text("Lendo da TV...") }
                } else if details.isEmpty {
                    Text("A TV nao respondeu em /api/v2/ agora.")
                }
                ForEach(Array(details.enumerated()), id: \.offset) { item in
                    HStack {
                        Text(item.element.0).foregroundStyle(.secondary)
                        Spacer()
                        Text(item.element.1).multilineTextAlignment(.trailing)
                    }
                    .font(.subheadline)
                }
            }
            .navigationTitle("A TV")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Fechar") { showInfo = false }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }
}

/// Tecla retangular dos painéis.
struct PadKey: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(LinearGradient(colors: [keyFace, keyEdge],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .stroke(Color.white.opacity(0.10), lineWidth: 1))
            )
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .shadow(color: .black.opacity(configuration.isPressed ? 0.25 : 0.45),
                    radius: configuration.isPressed ? 1 : 3, y: configuration.isPressed ? 1 : 2)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}

/// Sem fundo proprio: a cor ja desenha o relevo.
struct PlainButtonStyle3D: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.9 : 1)
            .brightness(configuration.isPressed ? 0.1 : 0)
            .animation(.easeOut(duration: 0.09), value: configuration.isPressed)
    }
}
