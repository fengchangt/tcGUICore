#include "ConfigFile.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcGUICore {
namespace {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json* child(const char* key) const
    {
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }
    std::string asString() const
    {
        if (type == String) {
            return s;
        }
        if (type == Number) {
            std::ostringstream oss;
            oss << n;
            return oss.str();
        }
        return {};
    }
    double asNumber(double def = 0) const { return type == Number ? n : def; }
    bool asBool(bool def = false) const { return type == Bool ? b : def; }
};

class Parser {
public:
    explicit Parser(std::string t)
        : t_(std::move(t))
    {
    }

    Json parse()
    {
        skip();
        Json v = value();
        skip();
        return v;
    }

private:
    std::string t_;
    std::size_t i_ = 0;

    void skip()
    {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) {
            ++i_;
        }
    }
    char peek() const { return i_ < t_.size() ? t_[i_] : '\0'; }
    char get() { return i_ < t_.size() ? t_[i_++] : '\0'; }

    Json value()
    {
        skip();
        const char c = peek();
        if (c == '{') {
            return object();
        }
        if (c == '[') {
            return array();
        }
        if (c == '"') {
            Json j;
            j.type = Json::String;
            j.s = string();
            return j;
        }
        if (c == 't' || c == 'f') {
            return boolean();
        }
        if (c == 'n') {
            return nullv();
        }
        return number();
    }

    Json object()
    {
        get();
        Json j;
        j.type = Json::Object;
        skip();
        if (peek() == '}') {
            get();
            return j;
        }
        while (true) {
            skip();
            const std::string key = string();
            skip();
            if (get() != ':') {
                throw std::runtime_error("JSON: expected ':'");
            }
            j.o[key] = value();
            skip();
            const char c = get();
            if (c == '}') {
                break;
            }
            if (c != ',') {
                throw std::runtime_error("JSON: expected ',' or '}'");
            }
        }
        return j;
    }

    Json array()
    {
        get();
        Json j;
        j.type = Json::Array;
        skip();
        if (peek() == ']') {
            get();
            return j;
        }
        while (true) {
            j.a.push_back(value());
            skip();
            const char c = get();
            if (c == ']') {
                break;
            }
            if (c != ',') {
                throw std::runtime_error("JSON: expected ',' or ']'");
            }
        }
        return j;
    }

    std::string string()
    {
        if (get() != '"') {
            throw std::runtime_error("JSON: expected string");
        }
        std::string out;
        while (i_ < t_.size()) {
            char c = get();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                const char e = get();
                if (e == '"' || e == '\\' || e == '/') {
                    out += e;
                } else if (e == 'n') {
                    out += '\n';
                } else if (e == 't') {
                    out += '\t';
                } else {
                    out += e;
                }
            } else {
                out += c;
            }
        }
        throw std::runtime_error("JSON: unterminated string");
    }

    Json number()
    {
        const std::size_t start = i_;
        if (peek() == '-') {
            get();
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            get();
        }
        if (peek() == '.') {
            get();
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                get();
            }
        }
        Json j;
        j.type = Json::Number;
        j.n = std::stod(t_.substr(start, i_ - start));
        return j;
    }

    Json boolean()
    {
        Json j;
        j.type = Json::Bool;
        if (t_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            j.b = true;
            return j;
        }
        if (t_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            j.b = false;
            return j;
        }
        throw std::runtime_error("JSON: expected bool");
    }

    Json nullv()
    {
        if (t_.compare(i_, 4, "null") != 0) {
            throw std::runtime_error("JSON: expected null");
        }
        i_ += 4;
        return {};
    }
};

LocalizedText readLocalized(const Json& node)
{
    LocalizedText t;
    if (node.type == Json::String) {
        t.zh = node.s;
        t.en = node.s;
        return t;
    }
    if (node.type == Json::Object) {
        if (const Json* zh = node.child("zh")) {
            t.zh = zh->asString();
        }
        if (const Json* en = node.child("en")) {
            t.en = en->asString();
        }
        if (const Json* cn = node.child("cn")) {
            t.zh = cn->asString();
        }
    }
    return t;
}

ValueType parseType(const std::string& s)
{
    if (s == "bool" || s == "BOOL") {
        return ValueType::Bool;
    }
    if (s == "int8" || s == "SINT") {
        return ValueType::Int8;
    }
    if (s == "uint8" || s == "byte" || s == "BYTE" || s == "USINT") {
        return ValueType::UInt8;
    }
    if (s == "int16" || s == "int" || s == "INT") {
        return ValueType::Int16;
    }
    if (s == "uint16" || s == "WORD" || s == "UINT") {
        return ValueType::UInt16;
    }
    if (s == "int32" || s == "DINT") {
        return ValueType::Int32;
    }
    if (s == "uint32" || s == "DWORD" || s == "UDINT") {
        return ValueType::UInt32;
    }
    if (s == "int64" || s == "LINT") {
        return ValueType::Int64;
    }
    if (s == "float" || s == "REAL") {
        return ValueType::Float;
    }
    if (s == "double" || s == "LREAL") {
        return ValueType::Double;
    }
    throw std::runtime_error("unknown variable type: " + s);
}

IoDirection parseDirection(const std::string& s)
{
    if (s == "out" || s == "output" || s == "write" || s == "toPlc") {
        return IoDirection::Output;
    }
    if (s == "in" || s == "input" || s == "read" || s == "fromPlc") {
        return IoDirection::Input;
    }
    throw std::runtime_error("direction must be input or output (C++ view): " + s);
}

} // namespace

bool loadPlcConfigFile(const std::string& path, LoadedConfig& out, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    try {
        const Json root = Parser(text).parse();
        if (root.type != Json::Object) {
            throw std::runtime_error("root must be object");
        }
        if (const Json* w = root.child("window")) {
            if (const Json* width = w->child("width")) {
                out.window.width = static_cast<int>(width->asNumber(kDefaultWindowWidth));
            }
            if (const Json* height = w->child("height")) {
                out.window.height = static_cast<int>(height->asNumber(kDefaultWindowHeight));
            }
            if (const Json* lang = w->child("language")) {
                const std::string ls = lang->asString();
                out.window.language = (ls == "en" || ls == "EN") ? Language::En : Language::Zh;
            }
        }
        const Json* plcs = root.child("plcs");
        if (!plcs || plcs->type != Json::Array) {
            throw std::runtime_error("missing plcs[]");
        }
        for (const Json& p : plcs->a) {
            PlcConfig plc;
            if (const Json* id = p.child("id")) {
                plc.id = id->asString();
            }
            if (const Json* dn = p.child("displayName")) {
                plc.displayName = readLocalized(*dn);
            } else {
                plc.displayName.zh = plc.id;
                plc.displayName.en = plc.id;
            }
            if (const Json* ip = p.child("ip")) {
                plc.ip = ip->asString();
            }
            if (const Json* port = p.child("adsPort")) {
                plc.adsPort = static_cast<uint16_t>(port->asNumber(851));
            }
            if (const Json* to = p.child("timeoutMs")) {
                plc.timeoutMs = static_cast<uint32_t>(to->asNumber(2000));
            }
            if (plc.id.empty() || plc.ip.empty()) {
                throw std::runtime_error("each PLC needs id and ip");
            }
            plc.amsNetId = amsNetIdFromIp(plc.ip);
            plc.configAmsNetId = plc.amsNetId;
            out.plcs.push_back(plc);

            const Json* vars = p.child("variables");
            if (!vars) {
                continue;
            }
            if (vars->type != Json::Array) {
                throw std::runtime_error("variables must be array");
            }
            for (const Json& v : vars->a) {
                SymbolConfig sym;
                sym.plcId = plc.id;
                sym.amsNetId = plc.configAmsNetId;
                if (const Json* name = v.child("name")) {
                    sym.name = name->asString();
                }
                if (const Json* symbol = v.child("symbol")) {
                    sym.path = symbol->asString();
                }
                if (const Json* type = v.child("type")) {
                    sym.type = parseType(type->asString());
                }
                if (const Json* dir = v.child("direction")) {
                    sym.direction = parseDirection(dir->asString());
                } else {
                    throw std::runtime_error("variable " + sym.name + " missing direction (input|output)");
                }
                if (const Json* lab = v.child("label")) {
                    sym.label = readLocalized(*lab);
                } else {
                    sym.label.zh = sym.name;
                    sym.label.en = sym.name;
                }
                if (sym.name.empty() || sym.path.empty()) {
                    throw std::runtime_error("variable needs name and symbol");
                }
                out.symbols.push_back(std::move(sym));
            }
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace tcGUICore
