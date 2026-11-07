import Foundation

struct TVDevice: Identifiable, Codable, Hashable {
    var id = UUID()
    var name: String
    var host: String
    var port: Int = 5555
    var model: String = ""
    var android: String = ""

    var label: String {
        if !model.isEmpty { return model }
        return name.isEmpty ? host : name
    }
}

enum TvKey {
    static let home = 3
    static let back = 4
    static let dpadUp = 19
    static let dpadDown = 20
    static let dpadLeft = 21
    static let dpadRight = 22
    static let dpadCenter = 23
    static let volumeUp = 24
    static let volumeDown = 25
    static let power = 26
    static let menu = 82
    static let playPause = 85
    static let mediaStop = 86
    static let mediaNext = 87
    static let mediaPrevious = 88
    static let mediaRewind = 89
    static let mediaFastForward = 90
    static let muteMic = 164
    static let mute = 91
    static let info = 165
    static let settings = 176
    static let tvInput = 178
    static let appSwitch = 187
    static let wakeup = 224
    static let sleep = 223
}

@MainActor
final class DeviceStore: ObservableObject {
    @Published var devices: [TVDevice] = []

    private let key = "anytv.devices"

    init() { load() }

    func load() {
        guard let data = UserDefaults.standard.data(forKey: key) else { return }
        if let decoded = try? JSONDecoder().decode([TVDevice].self, from: data) {
            devices = decoded
        }
    }

    func save() {
        if let data = try? JSONEncoder().encode(devices) {
            UserDefaults.standard.set(data, forKey: key)
        }
    }

    func upsert(_ device: TVDevice) {
        if let index = devices.firstIndex(where: { $0.host == device.host && $0.port == device.port }) {
            devices[index].name = device.name
            devices[index].model = device.model
            devices[index].android = device.android
        } else {
            devices.append(device)
        }
        save()
    }

    func remove(at offsets: IndexSet) {
        devices.remove(atOffsets: offsets)
        save()
    }
}

/// Pretty name for a package id: com.netflix.ninja -> Netflix Ninja
func prettifyPackage(_ package: String) -> String {
    var name = package
    if let dot = name.lastIndex(of: ".") {
        name = String(name[name.index(after: dot)...])
    }
    let cleaned = name.replacingOccurrences(of: "_", with: " ")
        .replacingOccurrences(of: "-", with: " ")
    return cleaned
        .split(separator: " ")
        .map { $0.prefix(1).uppercased() + $0.dropFirst() }
        .joined(separator: " ")
}
