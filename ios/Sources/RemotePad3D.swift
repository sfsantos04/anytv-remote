import SwiftUI

// Controle no formato do One Remote da Samsung: corpo escuro com brilho de
// acrilico, roda direcional em relevo e teclas com profundidade (sombra
// projetada + realce no topo). Cada tecla afunda ao toque e da retorno tatil.

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

struct RemotePad3D: View {
    let client: TVClient
    var title: String

    @State private var lastSent: String = ""

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        lastSent = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 18) {
            topRow
            wheel
            rockers
            bottomRow
            colorRow
            Text(lastSent.isEmpty ? " " : lastSent)
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.35))
                .frame(height: 12)
        }
        .padding(.horizontal, 22)
        .padding(.top, 20)
        .padding(.bottom, 26)
        .frame(maxWidth: 330)
        .background(body3D)
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
                // brilho de acrilico no alto
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

    // MARK: - Topo

    private var topRow: some View {
        HStack(spacing: 14) {
            Button {
                tap(.power, "Power")
            } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(Color(red: 0.95, green: 0.35, blue: 0.32))
                    .frame(width: 40, height: 40)
            }
            .buttonStyle(BevelButtonStyle(glow: Color.red.opacity(0.5)))

            Spacer(minLength: 0)

            VStack(spacing: 1) {
                Text(title)
                    .font(.system(size: 10, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white.opacity(0.55))
                    .lineLimit(1)
                Capsule()
                    .fill(Color.white.opacity(0.10))
                    .frame(width: 34, height: 3)
            }

            Spacer(minLength: 0)

            Button {
                tap(.input, "Fonte")
            } label: {
                Image(systemName: "rectangle.on.rectangle")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 40, height: 40)
            }
            .buttonStyle(BevelButtonStyle())
        }
    }

    // MARK: - Roda direcional

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
                .frame(width: 196, height: 196)

            VStack {
                wheelArrow("chevron.up", .up, "Cima")
                Spacer(minLength: 0)
                wheelArrow("chevron.down", .down, "Baixo")
            }
            .padding(.vertical, 10)
            .frame(height: 196)

            HStack {
                wheelArrow("chevron.left", .left, "Esquerda")
                Spacer(minLength: 0)
                wheelArrow("chevron.right", .right, "Direita")
            }
            .padding(.horizontal, 10)
            .frame(width: 196)

            Button {
                tap(.ok, "OK")
            } label: {
                ZStack {
                    Circle()
                        .fill(
                            LinearGradient(colors: [Color(white: 0.34), Color(white: 0.14)],
                                           startPoint: .topLeading, endPoint: .bottomTrailing)
                        )
                        .overlay(Circle().strokeBorder(Color.white.opacity(0.30), lineWidth: 1))
                    Text("OK")
                        .font(.system(size: 17, weight: .bold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.9))
                }
                .frame(width: 84, height: 84)
                .shadow(color: .black.opacity(0.55), radius: 5, x: 0, y: 4)
            }
            .buttonStyle(BevelButtonStyle(glow: Color.white.opacity(0.25)))
        }
        .frame(height: 200)
    }

    private func wheelArrow(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button {
            tap(cmd, label)
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(.white.opacity(0.78))
                .frame(width: 54, height: 54)
        }
        .buttonStyle(BevelButtonStyle())
    }

    // MARK: - Volume e canal

    private var rockers: some View {
        HStack(spacing: 26) {
            rocker(label: "VOL", up: .volUp, down: .volDown,
                   upIcon: "plus", downIcon: "minus")
            rocker(label: "CH", up: .chUp, down: .chDown,
                   upIcon: "chevron.up", downIcon: "chevron.down")
        }
    }

    private func rocker(label: String, up: Cmd, down: Cmd,
                        upIcon: String, downIcon: String) -> some View {
        VStack(spacing: 0) {
            Button {
                tap(up, "\(label) +")
            } label: {
                Image(systemName: upIcon)
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 46, height: 40)
            }
            .buttonStyle(BevelButtonStyle(shape: AnyShape(UnevenRoundedRectangle(
                topLeadingRadius: 23, bottomLeadingRadius: 0,
                bottomTrailingRadius: 0, topTrailingRadius: 23)), glow: .clear))

            Rectangle()
                .fill(Color.black.opacity(0.55))
                .frame(width: 46, height: 1)

            Button {
                tap(down, "\(label) -")
            } label: {
                Image(systemName: downIcon)
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 46, height: 40)
            }
            .buttonStyle(BevelButtonStyle(shape: AnyShape(UnevenRoundedRectangle(
                topLeadingRadius: 0, bottomLeadingRadius: 23,
                bottomTrailingRadius: 23, topTrailingRadius: 0)), glow: .clear))

            Text(label)
                .font(.system(size: 8, weight: .semibold))
                .foregroundStyle(.white.opacity(0.30))
                .padding(.top, 4)
        }
    }

    // MARK: - Linha inferior

    private var bottomRow: some View {
        HStack(spacing: 20) {
            smallKey("arrow.uturn.backward", .back, "Voltar")
            smallKey("house", .home, "Home")
            smallKey("playpause", .playPause, "Play/Pause")
            smallKey("speaker.slash", .mute, "Mudo")
            smallKey("info.circle", .info, "Info")
        }
    }

    private func smallKey(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button {
            tap(cmd, label)
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(.white.opacity(0.82))
                .frame(width: 46, height: 46)
        }
        .buttonStyle(BevelButtonStyle())
    }

    // MARK: - Teclas coloridas

    private var colorRow: some View {
        HStack(spacing: 18) {
            colorKey(Color(red: 0.80, green: 0.19, blue: 0.20), .red, "Vermelho")
            colorKey(Color(red: 0.28, green: 0.62, blue: 0.30), .green, "Verde")
            colorKey(Color(red: 0.88, green: 0.74, blue: 0.20), .yellow, "Amarelo")
            colorKey(Color(red: 0.20, green: 0.42, blue: 0.80), .blue, "Azul")
        }
    }

    private func colorKey(_ color: Color, _ cmd: Cmd, _ label: String) -> some View {
        Button {
            tap(cmd, label)
        } label: {
            Circle()
                .fill(
                    LinearGradient(colors: [color.opacity(0.95), color.opacity(0.55)],
                                   startPoint: .top, endPoint: .bottom)
                )
                .overlay(
                    Circle().fill(
                        LinearGradient(colors: [Color.white.opacity(0.45), .clear],
                                       startPoint: .topLeading, endPoint: .center)
                    )
                )
                .overlay(Circle().strokeBorder(Color.black.opacity(0.45), lineWidth: 1))
                .frame(width: 30, height: 30)
                .shadow(color: color.opacity(0.55), radius: 6, x: 0, y: 2)
        }
        .buttonStyle(PlainButtonStyle3D())
        .accessibilityLabel(label)
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
