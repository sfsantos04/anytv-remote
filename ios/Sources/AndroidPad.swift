import SwiftUI

// Controle no formato do TCL Android TV: corpo preto fosco, teclas claras
// ladeando a cruz direcional, balancim de volume alto no centro, CH+ / CH-
// empilhados, teclado e assistente à esquerda e as duas marcas no rodapé.

private let shellTop = Color(red: 0.11, green: 0.11, blue: 0.12)
private let shellBottom = Color(red: 0.04, green: 0.04, blue: 0.05)
private let darkKeyTop = Color(red: 0.22, green: 0.22, blue: 0.23)
private let darkKeyBottom = Color(red: 0.11, green: 0.11, blue: 0.12)
private let lightKeyTop = Color(red: 0.87, green: 0.87, blue: 0.86)
private let lightKeyBottom = Color(red: 0.68, green: 0.68, blue: 0.67)

struct AndroidPad: View {
    let client: TVClient
    var title: String
    @Binding var wantKeyboard: Bool

    @State private var lastSent = ""

    private func tap(_ cmd: Cmd, _ label: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        lastSent = label
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 14) {
            topRow
            crossWithSides
            lowerRow
            brandRow
            Spacer().frame(height: 52)   // área vazia da base, como no controle
            Text(lastSent.isEmpty ? " " : lastSent)
                .font(.caption2)
                .foregroundStyle(.white.opacity(0.30))
                .frame(height: 10)
        }
        .padding(.horizontal, 16)
        .padding(.top, 16)
        .padding(.bottom, 18)
        .frame(maxWidth: 320)
        .background(shell)
    }

    // MARK: - Corpo

    private var shell: some View {
        RoundedRectangle(cornerRadius: 36, style: .continuous)
            .fill(LinearGradient(colors: [shellTop, shellBottom],
                                 startPoint: .top, endPoint: .bottom))
            .overlay(
                RoundedRectangle(cornerRadius: 36, style: .continuous)
                    .stroke(LinearGradient(colors: [Color.white.opacity(0.18),
                                                    Color.white.opacity(0.03)],
                                           startPoint: .top, endPoint: .bottom),
                            lineWidth: 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 36, style: .continuous)
                    .fill(LinearGradient(colors: [Color.white.opacity(0.05), .clear],
                                         startPoint: .top, endPoint: .center))
                    .allowsHitTesting(false)
            )
            .shadow(color: .black.opacity(0.6), radius: 22, x: 0, y: 16)
    }

    // MARK: - Topo: power e ajustes

    private var topRow: some View {
        HStack {
            Button { tap(.power, "Power") } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(Color(red: 0.86, green: 0.26, blue: 0.26))
                    .frame(width: 46, height: 38)
            }
            .buttonStyle(DarkKey())
            .accessibilityLabel("Power")

            Spacer(minLength: 0)

            Text(title)
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .foregroundStyle(.white.opacity(0.35))
                .lineLimit(1)
                .frame(maxWidth: 100)

            Spacer(minLength: 0)

            Button { tap(.settings, "Ajustes") } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundStyle(.white.opacity(0.85))
                    .frame(width: 46, height: 38)
            }
            .buttonStyle(DarkKey())
            .accessibilityLabel("Ajustes")
        }
    }

    // MARK: - Cruz direcional: home/lista em cima, voltar/entrada embaixo

    private var crossWithSides: some View {
        HStack(alignment: .center, spacing: 8) {
            VStack(spacing: 8) {
                sideKey("house", .home, "Home")
                sideKey("arrow.uturn.backward", .back, "Voltar")
            }

            cross

            VStack(spacing: 8) {
                sideKey("list.bullet", .menu, "Lista")
                sideKey("rectangle.portrait.and.arrow.right", .input, "Entrada")
            }
        }
    }

    private var cross: some View {
        VStack(spacing: 0) {
            crossKey("chevron.up", .up, "Cima", [.topLeading, .topTrailing])
            HStack(spacing: 0) {
                crossKey("chevron.left", .left, "Esquerda", [.topLeading, .bottomLeading])
                Button { tap(.ok, "OK") } label: {
                    Circle()
                        .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                             startPoint: .top, endPoint: .bottom))
                        .overlay(Circle().stroke(Color.white.opacity(0.10), lineWidth: 1))
                        .frame(width: 46, height: 46)
                }
                .buttonStyle(PlainButtonStyle3D())
                .accessibilityLabel("OK")
                crossKey("chevron.right", .right, "Direita", [.topTrailing, .bottomTrailing])
            }
            crossKey("chevron.down", .down, "Baixo", [.bottomLeading, .bottomTrailing])
        }
    }

    private func crossKey(_ symbol: String, _ cmd: Cmd, _ label: String,
                          _ corners: [Corner]) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(.white.opacity(0.85))
                .frame(width: 46, height: 46)
                .background(
                    UnevenRoundedRectangle(topLeadingRadius: corners.contains(.topLeading) ? 11 : 0,
                                           bottomLeadingRadius: corners.contains(.bottomLeading) ? 11 : 0,
                                           bottomTrailingRadius: corners.contains(.bottomTrailing) ? 11 : 0,
                                           topTrailingRadius: corners.contains(.topTrailing) ? 11 : 0,
                                           style: .continuous)
                        .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                             startPoint: .top, endPoint: .bottom))
                        .overlay(
                            UnevenRoundedRectangle(topLeadingRadius: corners.contains(.topLeading) ? 11 : 0,
                                                   bottomLeadingRadius: corners.contains(.bottomLeading) ? 11 : 0,
                                                   bottomTrailingRadius: corners.contains(.bottomTrailing) ? 11 : 0,
                                                   topTrailingRadius: corners.contains(.topTrailing) ? 11 : 0,
                                                   style: .continuous)
                                .stroke(Color.white.opacity(0.08), lineWidth: 1)
                        )
                )
        }
        .buttonStyle(PlainButtonStyle3D())
        .accessibilityLabel(label)
    }

    enum Corner { case topLeading, topTrailing, bottomLeading, bottomTrailing }

    // MARK: - Assistente, volume e canais

    private var lowerRow: some View {
        HStack(alignment: .center, spacing: 8) {
            VStack(spacing: 8) {
                Button { tap(.voice, "Assistente") } label: {
                    ZStack {
                        RoundedRectangle(cornerRadius: 9, style: .continuous)
                            .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                                 startPoint: .top, endPoint: .bottom))
                            .frame(width: 46, height: 34)
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

                Button {
                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    wantKeyboard = true
                } label: {
                    Image(systemName: "keyboard")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(.white.opacity(0.85))
                        .frame(width: 46, height: 34)
                }
                .buttonStyle(DarkKey())
                .accessibilityLabel("Teclado")

                tinyDots
            }
            .frame(maxWidth: .infinity)

            volumeRocker

            VStack(spacing: 8) {
                Button { tap(.chUp, "CH+") } label: {
                    Text("CH+")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.88))
                        .frame(width: 54, height: 34)
                }
                .buttonStyle(DarkKey())

                Button { tap(.chDown, "CH-") } label: {
                    Text("CH-")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white.opacity(0.88))
                        .frame(width: 54, height: 34)
                }
                .buttonStyle(DarkKey())
            }
            .frame(maxWidth: .infinity)
        }
    }

    /// Os quatro pontinhos do controle, aqui só como marca visual.
    private var tinyDots: some View {
        HStack(spacing: 3) {
            Circle().fill(Color(red: 0.85, green: 0.24, blue: 0.24)).frame(width: 4, height: 4)
            Circle().fill(Color(red: 0.30, green: 0.65, blue: 0.33)).frame(width: 4, height: 4)
            Circle().fill(Color(red: 0.90, green: 0.78, blue: 0.24)).frame(width: 4, height: 4)
            Circle().fill(Color(red: 0.25, green: 0.45, blue: 0.82)).frame(width: 4, height: 4)
        }
        .frame(height: 8)
    }

    private var volumeRocker: some View {
        VStack(spacing: 0) {
            Button { tap(.volUp, "Volume +") } label: {
                Image(systemName: "plus")
                    .font(.system(size: 16, weight: .bold))
                    .foregroundStyle(.white.opacity(0.9))
                    .frame(width: 48, height: 42)
            }
            .buttonStyle(PlainButtonStyle3D())

            Image(systemName: "speaker.wave.1")
                .font(.system(size: 13))
                .foregroundStyle(.white.opacity(0.55))
                .frame(width: 48, height: 20)

            Button { tap(.volDown, "Volume -") } label: {
                Image(systemName: "minus")
                    .font(.system(size: 16, weight: .bold))
                    .foregroundStyle(.white.opacity(0.9))
                    .frame(width: 48, height: 42)
            }
            .buttonStyle(PlainButtonStyle3D())
        }
        .background(
            Capsule()
                .fill(LinearGradient(colors: [darkKeyTop, darkKeyBottom],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(Capsule().stroke(Color.white.opacity(0.10), lineWidth: 1))
                .shadow(color: .black.opacity(0.45), radius: 3, y: 2)
        )
        .accessibilityLabel("Volume")
    }

    // MARK: - Marcas: uma peça só, Netflix à esquerda e Globoplay à direita

    private var brandRow: some View {
        HStack(spacing: 0) {
            Button { launch("netflix", "Netflix") } label: {
                Text("NETFLIX")
                    .font(.system(size: 13, weight: .heavy, design: .rounded))
                    .foregroundStyle(Color(red: 0.90, green: 0.10, blue: 0.14))
                    .frame(maxWidth: .infinity)
                    .frame(height: 40)
                    .background(Color.white)
            }
            .buttonStyle(PlainButtonStyle3D())

            Button { launch("globoplay", "Globoplay") } label: {
                Text("globoplay")
                    .font(.system(size: 12, weight: .bold, design: .rounded))
                    .foregroundStyle(.white)
                    .frame(maxWidth: .infinity)
                    .frame(height: 40)
                    .background(Color(red: 0.94, green: 0.30, blue: 0.13))
            }
            .buttonStyle(PlainButtonStyle3D())
        }
        .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
        .shadow(color: .black.opacity(0.4), radius: 3, y: 2)
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

    private func sideKey(_ symbol: String, _ cmd: Cmd, _ label: String) -> some View {
        Button { tap(cmd, label) } label: {
            Image(systemName: symbol)
                .font(.system(size: 14, weight: .medium))
                .foregroundStyle(Color(white: 0.18))
                .frame(width: 46, height: 38)
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
            .shadow(color: configuration.isPressed ? .black.opacity(0.25) : .black.opacity(0.45),
                    radius: configuration.isPressed ? 1 : 3, y: configuration.isPressed ? 1 : 2)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}
