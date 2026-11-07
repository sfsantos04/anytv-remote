import SwiftUI

struct AppsSheet: View {
    let client: TVClient
    let canLaunch: Bool

    @Environment(\.dismiss) private var dismiss
    @State private var apps: [(String, String)] = []
    @State private var loading = true
    @State private var message = ""

    var body: some View {
        NavigationStack {
            List {
                if !message.isEmpty {
                    Text(message).font(.footnote).foregroundStyle(.secondary)
                }
                ForEach(apps, id: \.0) { app in
                    Button {
                        launch(app.0)
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(app.1.isEmpty ? prettifyName(app.0) : app.1)
                                .foregroundStyle(.primary)
                            Text(app.0).font(.caption2).foregroundStyle(.secondary)
                        }
                    }
                    .disabled(!canLaunch)
                }
            }
            .overlay {
                if loading { ProgressView("Lendo apps da TV...") }
            }
            .navigationTitle("Apps da TV")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Fechar") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button { Task { await load() } } label: { Image(systemName: "arrow.clockwise") }
                }
            }
            .task { await load() }
        }
    }

    private func load() async {
        loading = true
        message = ""
        do {
            let list = try await client.apps()
            apps = list
            loading = false
            if list.isEmpty { message = "A TV nao devolveu nenhum app." }
        } catch {
            loading = false
            message = "Falhou: \(error.localizedDescription)"
        }
    }

    private func launch(_ appID: String) {
        guard canLaunch else {
            message = "Este protocolo nao permite abrir app pelo controle."
            return
        }
        Task {
            do {
                try await client.launch(appID)
                message = "Abrindo na TV."
            } catch {
                message = "Falhou: \(error.localizedDescription)"
            }
        }
    }
}
