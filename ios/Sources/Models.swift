import Foundation

enum TVProto: Int, Codable, CaseIterable, Identifiable {
    case adb = 0
    case roku = 1
    case webos = 2
    case tizen = 3

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .adb: return "Android TV (ADB)"
        case .roku: return "Roku (ECP)"
        case .webos: return "LG webOS"
        case .tizen: return "Samsung Tizen"
        }
    }

    var short: String {
        switch self {
        case .adb: return "Android"
        case .roku: return "Roku"
        case .webos: return "LG"
        case .tizen: return "Samsung"
        }
    }

    var port: Int {
        switch self {
        case .adb: return 5555
        case .roku: return 8060
        case .webos: return 3000
        case .tizen: return 8002
        }
    }

    var setup: String {
        switch self {
        case .adb:
            return "Ajustes > Sobre > 7 toques em Compilacao > Depuracao USB/rede ligadas. "
                 + "A TV pede autorizacao na primeira conexao."
        case .roku:
            return "Nao precisa configurar nada: so estar na mesma rede."
        case .webos:
            return "A TV mostra um pedido de pareamento: aceite com o controle. "
                 + "Configuracoes > Conexao > LG Connect Apps ligado."
        case .tizen:
            return "A TV mostra 'Permitir' ao conectar: aceite com o controle. "
                 + "O canal seguro e a porta 8002 (wss); a 8001 responde e fecha com "
                 + "'notack'. Menu > Geral > Rede > Estado do dispositivo ligado."
        }
    }
}

struct TVDevice: Identifiable, Codable, Hashable {
    var id = UUID()
    var name: String
    var host: String
    var port: Int = 5555
    var proto: TVProto = .adb
    var token: String = ""
    var mac: String = ""
    var model: String = ""
    var android: String = ""

    var label: String {
        if !model.isEmpty { return model }
        return name.isEmpty ? host : name
    }

    private enum CodingKeys: String, CodingKey {
        case id, name, host, port, proto, token, mac, model, android
    }

    init(name: String, host: String, port: Int, proto: TVProto) {
        self.name = name
        self.host = host
        self.port = port
        self.proto = proto
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(UUID.self, forKey: .id) ?? UUID()
        name = try container.decodeIfPresent(String.self, forKey: .name) ?? ""
        host = try container.decodeIfPresent(String.self, forKey: .host) ?? ""
        port = try container.decodeIfPresent(Int.self, forKey: .port) ?? 5555
        proto = try container.decodeIfPresent(TVProto.self, forKey: .proto) ?? .adb
        token = try container.decodeIfPresent(String.self, forKey: .token) ?? ""
        mac = try container.decodeIfPresent(String.self, forKey: .mac) ?? ""
        model = try container.decodeIfPresent(String.self, forKey: .model) ?? ""
        android = try container.decodeIfPresent(String.self, forKey: .android) ?? ""
    }
}

/// Canonical commands; raw values must match anytv_cmd in anytv_remote.h.
enum Cmd: Int32 {
    case up = 0, down, left, right, ok, back, home, menu
    case volUp, volDown, mute, playPause, next, prev
    case stop, rewind, forward, power, info, settings, input, chUp, chDown
}

enum Cap {
    static let key: UInt32 = 1 << 0
    static let text: UInt32 = 1 << 1
    static let apps: UInt32 = 1 << 2
    static let launch: UInt32 = 1 << 3
    static let screen: UInt32 = 1 << 4
    static let powerOff: UInt32 = 1 << 5
    static let powerOn: UInt32 = 1 << 6
    static let info: UInt32 = 1 << 7
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
        if let index = devices.firstIndex(where: { $0.host == device.host && $0.proto == device.proto }) {
            devices[index].name = device.name
            if !device.model.isEmpty { devices[index].model = device.model }
            if !device.android.isEmpty { devices[index].android = device.android }
            if !device.token.isEmpty { devices[index].token = device.token }
            if !device.mac.isEmpty { devices[index].mac = device.mac }
            devices[index].port = device.port
        } else {
            devices.append(device)
        }
        save()
    }

    func update(_ device: TVDevice) {
        if let index = devices.firstIndex(where: { $0.id == device.id }) {
            devices[index] = device
            save()
        }
    }

    func remove(at offsets: IndexSet) {
        devices.remove(atOffsets: offsets)
        save()
    }
}

func prettifyName(_ raw: String) -> String {
    var name = raw
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
