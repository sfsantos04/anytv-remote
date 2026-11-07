import SwiftUI

// Controle no formato do TCL Android TV: corpo preto fosco, teclas claras nas
// laterais (home, lista, voltar, entrada), cruz direcional escura, balancim de
// volume alto no centro, CH+ / CH- empilhados e as teclas de marca no rodape.

private let shellTop = Color(red: 0.10, green: 0.10, blue: 0.11)
private let shellBottom = Color(red: 0.04, green: 0.04, blue: 0.05)
private let darkKeyTop = Color(red: 0.22, green: 0.22, blue: 0.23)
private let darkKeyBottom = Color(red: 0.11, green: 0.11, blue: 0.12)
private let lightKeyTop = Color(red: 0.86, green: 0.86, blue: 0.85)
private let lightKeyBottom = Color(red: 0.66, green: 0.66, blue: 0.65)

struct AndroidPad: View {
    let client: TVClient
    var title: String
    @Binding var wantKeyboard: Bool

    @State private var lastSent = ""
    @State private var showColors = false

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        lastSent = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 12) {
            topRow
            dpadWithSides
            volumeRow
            if showColors { colorRow }
            brandRow
            Text(lastSent.isEmpty ? " " : lastSent)
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.30))
                .frame(height: 10)
        }
        .padding(.horizontal, 18)
        .padding(.top, 18)
        .padding(.bottom, 22)
        .frame(maxWidth: 320)
        .background(shell)
    }

    // MARK: - Corpo

    private var shell: some View {
        RoundedRectangle(cornerRadius: 34, style: .continuous)
            .fill(LinearGradient(colors: [shellTop, shellBottom],
                                 startPoint: .top, endPoint: .bottom))
            .overlay(
                RoundedRectangle(cornerRadius: 34, style: .continuous)
                    .stroke(LinearGradient(colors: [Color.white.opacity(0.16),
                                                    Color.white.opacity(0.03)],
                                           startPoint: .top, endPoint: .bottom),
                            lineWidth: 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 34, style: .continuous)
                    .fill(LinearGradient(colors: [Color.white.opacity(0.06), .clear],
                                         startPoint: .top, endPoint: .center))
                    .allowsHitTesting(false)
            )
            .shadow(color: .black.opacity(0.6), radius: 20, x: 0, y: 14)
    }

    // MARK: - Topo: power e ajustes

    private var topRow: some View {
        HStack {
            Button { tap(.power, "Power") } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(Color(red: 0.85, green: 0.24, blue: 0.24))
                    .frame(width: 44, height: 34)
            }
            .buttonStyle(DarkKey())
            Spacer()
            Button { tap(.settings, "Ajustes") } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 44, height: 34)
            }
            .buttonStyle(DarkKey())
        }
    }

    // MARK: - Direcional com as teclas laterais

    private var dpadWithSides: some View {
        HStack(alignment: .center, spacing: 10) {
            VStack(spacing: 12) {
                sideKey("house", .home, "Home")
                sideKey("arrow.uturn.backward", .back, "Voltar")
                sideKey("keyboard", nil, "Teclado") { wantKeyboard = true }
            }
            Spacer(minLength: 0)
            cross
            Spacer(minLength: 0)
            VStack(spacing: 12) {
                sideKey("list.bullet", .menu, "Lista")
                sideKey("rectangle.portrait.and.arrow.right", .input, "Entrada")
                Button { withAnimation(.easeInOut(duration: 0.15)) { showColors.toggle() } } label: {
                    HStack(spacing: 3) {
                        Circle().fill(Color(red: 0.85, green: 0.24, blue: 0.24)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.30, green: 0.65, blue: 0.33)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.90, green: 0.78, blue: 0.24)).frame(width: 5, height: 5)
                        Circle().fill(Color(red: 0.25, green: 0.45, blue: 0.82)).frame(width: 5, height: 5)
                    }
                    .frame(width: 44, height: 34)
                }
                .buttonStyle(LightKey())
                .accessibilityLabel("Teclas coloridas")
            }
        }
    }

    private var cross: some View {
        VStack(spacing: 0) {
            dpadButton("chevron.up", .up, "Cima", corners: [.topLeading, .topTrailing])
            HStack(spacing: 0) {
                dpadButton("chevron.left", .left, "Esquerda", corners: [.topLeading, .bottomLeading])
                Button { tap(.ok, "OK") } label: {
                    Circle()
                        .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                             startPoint: .top, endPoint: .bottom))
                        .overlay(Circle().stroke(Color.white.opacity(0.10), lineWidth: 1))
                        .frame(width: 44, height: 44)
                }
                .buttonStyle(PlainButtonStyle3D())
                dpadButton("chevron.right", .right, "Direita", corners: [.topTrailing, .bottomTrailing])
            }
            dpadButton("chevron.down", .down, "Baixo", corners: [.bottomLeading, .bottomTrailing])
        }
    }

    private func dpadButton(_ symbol: String, _ cmd: Cmd, _ label: String,
                            corners: [Corner]) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(.white.opacity(0.85))
                .frame(width: 44, height: 44)
                .background(
                    UnevenRoundedRectangle(topLeadingRadius: corners.contains(.topLeading) ? 10 : 0,
                                           bottomLeadingRadius: corners.contains(.bottomLeading) ? 10 : 0,
                                           bottomTrailingRadius: corners.contains(.bottomTrailing) ? 10 : 0,
                                           topTrailingRadius: corners.contains(.topTrailing) ? 10 : 0,
                                           style: .continuous)
                        .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                             startPoint: .top, endPoint: .bottom))
                        .overlay(
                            UnevenRoundedRectangle(topLeadingRadius: corners.contains(.topLeading) ? 10 : 0,
                                                   bottomLeadingRadius: corners.contains(.bottomLeading) ? 10 : 0,
                                                   bottomTrailingRadius: corners.contains(.bottomTrailing) ? 10 : 0,
                                                   topTrailingRadius: corners.contains(.topTrailing) ? 10 : 0,
                                                   style: .continuous)
                                .stroke(Color.white.opacity(0.08), lineWidth: 1)
                        )
                )
        }
        .buttonStyle(PlainButtonStyle3D())
    }

    enum Corner { case topLeading, topTrailing, bottomLeading, bottomTrailing }

    // MARK: - Volume, assistente e canais

    private var volumeRow: some View {
        HStack(spacing: 14) {
            Button { tap(.voice, "Assistente") } label: {
                ZStack {
                    Circle()
                        .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                             startPoint: .top, endPoint: .bottom))
                        .frame(width: 46, height: 46)
                    HStack(spacing: 2) {
                        Circle().fill(Color(red: 0.26, green: 0.52, blue: 0.96)).frame(width: 6, height: 6)
                        Circle().fill(Color(red: 0.92, green: 0.26, blue: 0.21)).frame(width: 6, height: 6)
                        Circle().fill(Color(red: 0.98, green: 0.78, blue: 0.18)).frame(width: 6, height: 6)
                        Circle().fill(Color(red: 0.20, green: 0.68, blue: 0.35)).frame(width: 6, height: 6)
                    }
                }
            }
            .buttonStyle(PlainButtonStyle3D())
            .accessibilityLabel("Assistente de voz")

            VStack(spacing: 0) {
                Button { tap(.volUp, "Volume +") } label: {
                    Image(systemName: "plus")
                        .font(.system(size: 15, weight: .bold))
                        .foregroundStyle(.white.opacity(0.9))
                        .frame(width: 46, height: 40)
                }
                .buttonStyle(PlainButtonStyle3D())

                Image(systemName: "speaker.wave.1")
                    .font(.system(size: 13))
                    .foregroundStyle(.white.opacity(0.55))
                    .frame(width: 46, height: 20)

                Button { tap(.volDown, "Volume -") } label: {
                    Image(systemName: "minus")
                        .font(.system(size: 15, weight: .bold))
                        .foregroundStyle(.white.opacity(0.9))
                        .frame(width: 46, height: 40)
                }
                .buttonStyle(PlainButtonStyle3D())
            }
            .background(
                Capsule()
                    .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(Capsule().stroke(Color.white.opacity(0.10), lineWidth: 1))
                    .shadow(color: .black.opacity(0.5), radius: 3, y: 2)
            )

            VStack(spacing: 10) {
                Button { tap(.chUp, "CH+") } label: {
                    Text("CH+")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.88))
                        .frame(width: 52, height: 36)
                }
                .buttonStyle(DarkKey())
                Button { tap(.chDown, "CH-") } label: {
                    Text("CH-")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.88))
                        .frame(width: 52, height: 36)
                }
                .buttonStyle(DarkKey())
            }
        }
    }

    private var colorRow: some View {
        HStack(spacing: 14) {
            colorKey(Color(red: 0.80, green: 0.19, blue: 0.20), .red, "Vermelho")
            colorKey(Color(red: 0.28, green: 0.62, blue: 0.30), .green, "Verde")
            colorKey(Color(red: 0.88, green: 0.74, blue: 0.20), .yellow, "Amarelo")
            colorKey(Color(red: 0.20, green: 0.42, blue: 0.80), .blue, "Azul")
        }
        .padding(.vertical, 2)
    }

    private func colorKey(_ color: Color, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Circle()
                .fill(LinearGradient(colors: [color, color.opacity(0.6)],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Circle().stroke(Color.black.opacity(0.4), lineWidth: 1))
                .frame(width: 28, height: 28)
                .shadow(color: color.opacity(0.5), radius: 4, y: 2)
        }
        .buttonStyle(PlainButtonStyle3D())
        .accessibilityLabel(label)
    }

    // MARK: - Marcas

    private var brandRow: some View {
        HStack(spacing: 8) {
            Button { launch("netflix", "Netflix") } label: {
                Text("NETFLIX")
                    .font(.system(size: 13, weight: .heavy, design: .rounded))
                    .foregroundStyle(Color(red: 0.90, green: 0.10, blue: 0.14))
                    .frame(maxWidth: .infinity)
                    .frame(height: 38)
                    .background(RoundedRectangle(cornerRadius: 6).fill(Color.white))
            }
            .buttonStyle(PlainButtonStyle3D())

            Button { launch("globoplay", "Globoplay") } label: {
                Text("globoplay")
                    .font(.system(size: 12, weight: .bold, design: .rounded))
                    .foregroundStyle(.white)
                    .frame(maxWidth: .infinity)
                    .frame(height: 38)
                    .background(RoundedRectangle(cornerRadius: 6)
                        .fill(Color(red: 0.94, green: 0.30, blue: 0.13)))
            }
            .buttonStyle(PlainButtonStyle3D())
        }
    }

    /// Abre o app pelo nome instalado no aparelho (cada TV usa um pacote).
    private func launch(_ needle: String, _ label: String) {
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        lastSent = label
        Task {
            guard let apps = try? await client.apps() else { return }
            let target = apps.first { $0.name.lowercased().contains(needle) }
                ?? apps.first { $0.id.lowercased().contains(needle) }
            if let target {
                try? await client.launch(target.id)
            } else {
                lastSent = "\(label) nao encontrado"
            }
        }
    }

    private func sideKey(_ symbol: String, _ cmd: Cmd?, _ label: String,
                         action: (() -> Void)? = nil) -> some View {
        Button {
            if let cmd { tap(cmd, label) } else { action?(); UIImpactFeedbackGenerator(style: .light).impactOccurred() }
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 14, weight: .medium))
                .foregroundStyle(Color(white: 0.18))
                .frame(width: 44, height: 34)
        }
        .buttonStyle(LightKey())
        .accessibilityLabel(label)
    }
}

// MARK: - Estilos das teclas

struct DarkKey: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .stroke(Color.white.opacity(0.09), lineWidth: 1))
            )
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .shadow(color: .black.opacity(configuration.isPressed ? 0.3 : 0.5),
                    radius: configuration.isPressed ? 1 : 3, y: configuration.isPressed ? 1 : 2)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}

struct LightKey: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(LinearGradient(colors: [lightKeyTop, lightKeyBottom],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .stroke(Color.white.opacity(0.5), lineWidth: 1))
            )
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .brightness(configuration.isPressed ? -0.08 : 0)
            .shadow(color: .black.opacity(configuration.isPressed ? 0.25 : 0.45),
                    radius: configuration.isPressed ? 1 : 3, y: configuration.isPressed ? 1 : 2)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}
