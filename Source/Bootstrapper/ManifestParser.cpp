#include "NativeUI.h"
#include <string_view>

static std::string ExtractStringField(std::string_view json, std::string_view key) {
    std::string search_key = "\"" + std::string(key) + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string_view::npos) return "";
    
    pos += search_key.length();
    // skip whitespaces
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    
    if (pos < json.length() && json[pos] == '"') {
        pos++;
        size_t end_pos = json.find('"', pos);
        if (end_pos != std::string_view::npos) {
            return std::string(json.substr(pos, end_pos - pos));
        }
    }
    return "";
}

RuntimeManifest ParseManifest(const std::string& json_content) {
    RuntimeManifest manifest;
    manifest.application_name = ExtractStringField(json_content, "name");
    manifest.version = ExtractStringField(json_content, "version");
    manifest.architecture = ExtractStringField(json_content, "architecture");
    manifest.executable = ExtractStringField(json_content, "executable");

    // Crude extraction of dependencies array
    size_t deps_start = json_content.find("\"dependencies\"");
    if (deps_start != std::string::npos) {
        size_t array_start = json_content.find('[', deps_start);
        size_t array_end = json_content.find(']', array_start);
        
        if (array_start != std::string::npos && array_end != std::string::npos) {
            std::string_view deps_str(json_content.data() + array_start, array_end - array_start);
            
            size_t current_pos = 0;
            while (true) {
                size_t obj_start = deps_str.find('{', current_pos);
                if (obj_start == std::string_view::npos) break;
                size_t obj_end = deps_str.find('}', obj_start);
                if (obj_end == std::string_view::npos) break;
                
                std::string_view obj_str = deps_str.substr(obj_start, obj_end - obj_start);
                
                Dependency dep;
                dep.file = ExtractStringField(std::string(obj_str), "file");
                
                size_t req_pos = obj_str.find("\"required\":");
                if (req_pos != std::string_view::npos) {
                    size_t true_pos = obj_str.find("true", req_pos);
                    dep.required = (true_pos != std::string_view::npos && true_pos < obj_str.find_first_of(",}", req_pos));
                } else {
                    dep.required = false;
                }
                
                if (!dep.file.empty()) {
                    manifest.dependencies.push_back(dep);
                }
                
                current_pos = obj_end + 1;
            }
        }
    }
    
    return manifest;
}
