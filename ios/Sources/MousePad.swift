import SwiftUI

// Touchpad: arrastar move o cursor da TV (vira setas), tocar dá OK.
// É o jeito que funciona em Samsung, Android, LG e Roku, porque todos navegam
// por foco — não existe injeção de ponteiro real em nenhum deles.

struct MousePad: View {
    let client: TVClient
    var onClose: () -> Void

    @State private var stepSize: CGFloat = 15
    @State private var base: CGSize = .zero
    @State private var lastSent = ""
    @State private var sendCount = 0

    private func send(_ cmd: Cmd, _ label: String) {
        lastSent = label
        sendCount += 1
        Task { try? await client.send(cmd) }
    }

    var body: some View {
        VStack(spacing: 14) {
            HStack {
                Label("Touchpad", systemImage: "cursorarrow.motionlines")
                    .font(.headline)
                Spacer()
                Button("Fechar", action: onClose)
            }

            ZStack {
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .fill(LinearGradient(colors: [Color(.secondarySystemBackground),
                                                  Color(.tertiarySystemBackground)],
                                         startPoint: .top, endPoint: .bottom))
                    .overlay(
                        RoundedRectangle(cornerRadius: 22, style: .continuous)
                            .stroke(Color.secondary.opacity(0.25), lineWidth: 1)
                    )

                VStack(spacing: 6) {
                    Image(systemName: "cursorarrow")
                        .font(.system(size: 30, weight: .light))
                        .foregroundStyle(.secondary)
                    Text("arraste para mover · toque para OK")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                }

                // área de gesto por cima de tudo
                Color.clear
                    .contentShape(Rectangle())
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onChanged { value in
                                drag(value.translation)
                            }
                            .onEnded { value in
                                let dist = hypot(value.translation.width, value.translation.height)
                                if dist < 8 {                       // toque curto = clique
                                    UIImpactFeedbackGenerator(style: .medium).impactOccurred()
                                    send(.ok, "OK")
                                }
                                base = .zero
                            }
                    )
            }
            .frame(height: 420)

            HStack(spacing: 12) {
                actionButton("OK", "checkmark") { send(.ok, "OK") }
                actionButton("Voltar", "arrow.uturn.backward") { send(.back, "Voltar") }
                actionButton("Home", "house") { send(.home, "Home") }
            }

            HStack(spacing: 12) {
                actionButton("Vol −", "speaker.minus") { send(.volDown, "Volume −") }
                actionButton("Mudo", "speaker.slash") { send(.mute, "Mudo") }
                actionButton("Vol +", "speaker.plus") { send(.volUp, "Volume +") }
            }

            Text(lastSent.isEmpty ? " " : "\(lastSent)  ·  \(sendCount) comandos")
                .font(.caption2)
                .foregroundStyle(.tertiary)
                .frame(height: 12)

            Spacer(minLength: 0)
        }
        .padding(18)
        .presentationDetents([.large])
        .onDisappear { onClose() }
    }

    /// Converte o arrasto em passos de seta, um a cada 15 pontos.
    private func drag(_ t: CGSize) {
        let step = stepSize
        while true {
            let dx = t.width - base.width
            let dy = t.height - base.height
            guard abs(dx) >= step || abs(dy) >= step else { break }
            if abs(dx) >= abs(dy) {
                if dx > 0 { send(.right, "→"); base.width += step }
                else { send(.left, "←"); base.width -= step }
            } else {
                if dy > 0 { send(.down, "↓"); base.height += step }
                else { send(.up, "↑"); base.height -= step }
            }
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        }
    }

    private func actionButton(_ title: String, _ icon: String,
                              action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 4) {
                Image(systemName: icon).font(.system(size: 16))
                Text(title).font(.system(size: 11, weight: .medium))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 54)
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color(.secondarySystemBackground)))
        }
        .buttonStyle(KeyPress())
    }
}
