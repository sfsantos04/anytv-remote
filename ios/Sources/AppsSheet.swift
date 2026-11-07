import SwiftUI

struct AppsSheet: View {
    let client: TVClient
    @Environment(\.dismiss) private var dismiss
    @State private var apps: [String] = []
    @State private var loading = true
    @State private var message = ""

    var body: some View {
        NavigationStack {
            List {
                if !message.isEmpty {
                    Text(message).font(.footnote).foregroundStyle(.secondary)
                }
                ForEach(apps, id: \.self) { package in
                    Button {
                        launch(package)
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(prettifyPackage(package)).foregroundStyle(.primary)
                            Text(package).font(.caption2).foregroundStyle(.secondary)
                        }
                    }
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
            if list.isEmpty {
                message = "A TV nao devolveu apps com activity de launcher."
            }
        } catch {
            loading = false
            message = "Falhou: \(error.localizedDescription)"
        }
    }

    private func launch(_ package: String) {
        Task {
            do {
                try await client.launch(package)
                message = "Abrindo \(prettifyPackage(package)) na TV."
            } catch {
                message = "Falhou: \(error.localizedDescription)"
            }
        }
    }
}
