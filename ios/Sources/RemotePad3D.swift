import SwiftUI

// Controle no formato do One Remote da Samsung. Topo: power (esquerda) e fonte
// (direita). Na mesma coluna, na altura das setas laterais do cursor, ficam o
// 123 e o voltar. Sem teclas coloridas.

private let graphiteTop = Color(red: 0.20, green: 0.22, blue: 0.24)
private let graphiteBottom = Color(red: 0.08, green: 0.09, blue: 0.10)
private let keyTop = Color(red: 0.30, green: 0.32, blue: 0.35)
private let keyBottom = Color(red: 0.15, green: 0.16, blue: 0.18)

/// Estilo de tecla com profundidade: realce em cima, sombra embaixo, afunda ao toque.
struct BevelButtonStyle: ButtonStyle {
    var shape: AnyShape = AnyShape(Circle())
    var glow: Color = .clear

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                shape
                    .fill(
                        LinearGradient(colors: [keyTop, keyBottom],
                                       startPoint: .top, endPoint: .bottom)
                    )
                    .overlay(
                        shape.stroke(
                            LinearGradient(colors: [Color.white.opacity(0.35),
                                                    Color.black.opacity(0.45)],
                                           startPoint: .top, endPoint: .bottom),
                            lineWidth: 1)
                    )
                    .overlay(
                        shape.fill(
                            LinearGradient(colors: [Color.white.opacity(configuration.isPressed ? 0.26 : 0.14),
                                                    Color.clear],
                                           startPoint: .top, endPoint: .center)
                        )
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
            .shadow(color: .white.opacity(configuration.isPressed ? 0.02 : 0.10),
                    radius: 1, x: 0, y: -1)
            .animation(.easeOut(duration: 0.09), value: configuration.isPressed)
    }
}

private let wheelSide: CGFloat = 188

struct RemotePad3D: View {
    let client: TVClient
    var title: String

    @State private var lastSent: String = ""
    @State private var showNumbers = false
    @State private var showInfo = false
    @State private var details: [(String, String)] = []
    @State private var loadingDetails = false

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        lastSent = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 18) {
            topRow
            wheelWithSides
            rockers
            if showNumbers { numberPad } else { bottomRow }
            Text(lastSent.isEmpty ? " " : lastSent)
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.35))
                .frame(height: 12)
        }
        .padding(.horizontal, 20)
        .padding(.top, 18)
        .padding(.bottom, 24)
        .frame(maxWidth: 330)
        .background(body3D)
        .sheet(isPresented: $showInfo) { infoSheet }
    }

    // MARK: - Corpo

    private var body3D: some View {
        RoundedRectangle(cornerRadius: 54, style: .continuous)
            .fill(
                LinearGradient(colors: [graphiteTop, graphiteBottom],
                               startPoint: .topLeading, endPoint: .bottomTrailing)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 54, style: .continuous)
                    .strokeBorder(
                        LinearGradient(colors: [Color.white.opacity(0.40),
                                                Color.white.opacity(0.05),
                                                Color.black.opacity(0.55)],
                                       startPoint: .topLeading, endPoint: .bottomTrailing),
                        lineWidth: 1.5)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 54, style: .continuous)
                    .fill(
                        LinearGradient(colors: [Color.white.opacity(0.14), .clear],
                                       startPoint: .top, endPoint: .center)
                    )
                    .blendMode(.plusLighter)
                    .allowsHitTesting(false)
            )
            .shadow(color: .black.opacity(0.55), radius: 26, x: 0, y: 18)
    }

    // MARK: - Topo: power e fonte

    private var topRow: some View {
        HStack(alignment: .center) {
            Button { tap(.power, "Power") } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(Color(red: 0.95, green: 0.35, blue: 0.32))
                    .frame(width: 44, height: 40)
            }
            .buttonStyle(BevelButtonStyle(glow: Color.red.opacity(0.5)))

            Spacer(minLength: 0)

            Text(title)
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.40))
                .lineLimit(1)
                .frame(maxWidth: 110)

            Spacer(minLength: 0)

            Button { tap(.input, "Fonte") } label: {
                Image(systemName: "rectangle.on.rectangle")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 44, height: 40)
            }
            .buttonStyle(BevelButtonStyle())
        }
    }

    // MARK: - Roda, com 123 e voltar na altura das setas laterais

    private var wheelWithSides: some View {
        HStack(alignment: .center, spacing: 6) {
            VStack {
                Spacer(minLength: 0)
                numberToggle
                Spacer(minLength: 0)
            }
            .frame(height: wheelSide)

            wheel

            VStack {
                Spacer(minLength: 0)
                Button { tap(.back, "Voltar") } label: {
                    Image(systemName: "arrow.uturn.backward")
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundStyle(.white.opacity(0.82))
                        .frame(width: 44, height: 34)
                }
                .buttonStyle(BevelButtonStyle(shape: AnyShape(RoundedRectangle(cornerRadius: 10, style: .continuous))))
                .accessibilityLabel("Voltar")
                Spacer(minLength: 0)
            }
            .frame(height: wheelSide)
        }
    }

    private var numberToggle: some View {
        Button {
            withAnimation(.easeInOut(duration: 0.15)) { showNumbers.toggle() }
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        } label: {
            Text(showNumbers ? "ABC" : "123")
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .foregroundStyle(.white.opacity(0.88))
                .frame(width: 44, height: 34)
        }
        .buttonStyle(BevelButtonStyle(shape: AnyShape(RoundedRectangle(cornerRadius: 10, style: .continuous)),
                                      glow: Color.white.opacity(0.18)))
        .accessibilityLabel("Teclado numerico")
    }

    private var wheel: some View {
        ZStack {
            Circle()
                .fill(
                    LinearGradient(colors: [Color(white: 0.26), Color(white: 0.10)],
                                   startPoint: .top, endPoint: .bottom)
                )
                .overlay(Circle().strokeBorder(Color.white.opacity(0.14), lineWidth: 1))
                .shadow(color: .black.opacity(0.7), radius: 10, x: 0, y: 6)
                .overlay(
                    Circle()
                        .strokeBorder(Color.white.opacity(0.06), lineWidth: 10)
                        .blur(radius: 6)
                        .offset(y: 2)
                )
                .frame(width: wheelSide, height: wheelSide)

            VStack {
                wheelArrow("chevron.up", .up, "Cima")
                Spacer(minLength: 0)
                wheelArrow("chevron.down", .down, "Baixo")
            }
            .padding(.vertical, 8)
            .frame(height: wheelSide)

            HStack {
                wheelArrow("chevron.left", .left, "Esquerda")
                Spacer(minLength: 0)
                wheelArrow("chevron.right", .right, "Direita")
            }
            .padding(.horizontal, 8)
            .frame(width: wheelSide)

            Button { tap(.ok, "OK") } label: {
                ZStack {
                    Circle()
                        .fill(
                            LinearGradient(colors: [Color(white: 0.34), Color(white: 0.14)],
                                           startPoint: .topLeading, endPoint: .bottomTrailing)
                        )
                        .overlay(Circle().strokeBorder(Color.white.opacity(0.30), lineWidth: 1))
                    Text("OK")
                        .font(.system(size: 16, weight: .bold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.9))
                }
                .frame(width: 78, height: 78)
                .shadow(color: .black.opacity(0.55), radius: 5, x: 0, y: 4)
            }
            .buttonStyle(BevelButtonStyle(glow: Color.white.opacity(0.25)))
            .accessibilityLabel("OK")
        }
        .frame(height: wheelSide)
    }

    private func wheelArrow(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(.white.opacity(0.78))
                .frame(width: 52, height: 52)
        }
        .buttonStyle(BevelButtonStyle())
        .accessibilityLabel(label)
    }

    // MARK: - Volume e canal

    private var rockers: some View {
        HStack(spacing: 40) {
            rocker(label: "VOL", up: .volUp, down: .volDown, upIcon: "plus", downIcon: "minus")
            rocker(label: "CH", up: .chUp, down: .chDown, upIcon: "chevron.up", downIcon: "chevron.down")
        }
    }

    private func rocker(label: String, up: Cmd, down: Cmd,
                        upIcon: String, downIcon: String) -> some View {
        VStack(spacing: 0) {
            Button { tap(up, "\(label) +") } label: {
                Image(systemName: upIcon)
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 46, height: 38)
            }
            .buttonStyle(BevelButtonStyle(shape: AnyShape(UnevenRoundedRectangle(
                topLeadingRadius: 22, bottomLeadingRadius: 0,
                bottomTrailingRadius: 0, topTrailingRadius: 22))))
            .accessibilityLabel("\(label) mais")

            Rectangle().fill(Color.black.opacity(0.55)).frame(width: 46, height: 1)

            Button { tap(down, "\(label) -") } label: {
                Image(systemName: downIcon)
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 46, height: 38)
            }
            .buttonStyle(BevelButtonStyle(shape: AnyShape(UnevenRoundedRectangle(
                topLeadingRadius: 0, bottomLeadingRadius: 22,
                bottomTrailingRadius: 22, topTrailingRadius: 0))))
            .accessibilityLabel("\(label) menos")

            Text(label)
                .font(.system(size: 8, weight: .semibold))
                .foregroundStyle(.white.opacity(0.30))
                .padding(.top, 4)
        }
    }

    // MARK: - Linha inferior

    private var bottomRow: some View {
        HStack(spacing: 22) {
            smallKey("house", .home, "Home")
            smallKey("playpause", .playPause, "Play/Pause")
            smallKey("speaker.slash", .mute, "Mudo")
            infoKey
        }
    }

    private func smallKey(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(.white.opacity(0.82))
                .frame(width: 48, height: 48)
        }
        .buttonStyle(BevelButtonStyle())
        .accessibilityLabel(label)
    }

    /// O painel mostra o que a TV informa de verdade. Segurar manda KEY_INFO.
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
                .foregroundStyle(.white.opacity(0.82))
                .frame(width: 48, height: 48)
        }
        .buttonStyle(BevelButtonStyle())
        .simultaneousGesture(LongPressGesture(minimumDuration: 0.7).onEnded { _ in
            tap(.info, "Info na TV")
        })
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

    // MARK: - Teclado numerico

    private var numberPad: some View {
        VStack(spacing: 10) {
            ForEach([[1, 2, 3], [4, 5, 6], [7, 8, 9]], id: \.self) { line in
                HStack(spacing: 18) {
                    ForEach(line, id: \.self) { digit in numberKey(digit) }
                }
            }
            HStack(spacing: 18) {
                numKey("CH-", .chDown)
                numberKey(0)
                numKey("CH+", .chUp)
            }
        }
        .padding(.vertical, 2)
    }

    private func numberKey(_ digit: Int) -> some View {
        let cmd = Cmd(rawValue: Cmd.num0.rawValue + Int32(digit)) ?? .num0
        return Button { tap(cmd, "\(digit)") } label: {
            Text("\(digit)")
                .font(.system(size: 17, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.92))
                .frame(width: 54, height: 38)
        }
        .buttonStyle(BevelButtonStyle(shape: AnyShape(RoundedRectangle(cornerRadius: 11, style: .continuous))))
        .accessibilityLabel("Numero \(digit)")
    }

    private func numKey(_ label: String, _ cmd: Cmd) -> some View {
        Button { tap(cmd, label) } label: {
            Text(label)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.75))
                .frame(width: 54, height: 38)
        }
        .buttonStyle(BevelButtonStyle(shape: AnyShape(RoundedRectangle(cornerRadius: 11, style: .continuous))))
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
