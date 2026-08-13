#include "ISearchEngine.h"
#include "Config.h"
#include "WalkEngine.h"
#include "FdEngine.h"
#include "EverythingEngine.h"
#include "MftEngine.h"

namespace ac {

std::string engineKey(EngineType t) {
    switch (t) {
        case EngineType::Walk:       return "walk";
        case EngineType::Fdfind:     return "fdfind";
        case EngineType::Everything: return "everything";
        case EngineType::Mft:        return "mft";
    }
    return "walk";
}

EngineType engineFromKey(const std::string& key) {
    if (key == "fdfind")     return EngineType::Fdfind;
    if (key == "everything") return EngineType::Everything;
    if (key == "mft")        return EngineType::Mft;
    return EngineType::Walk;
}

std::unique_ptr<ISearchEngine> createEngine(EngineType t, const Config& cfg) {
    switch (t) {
        case EngineType::Fdfind:     return std::make_unique<FdEngine>(cfg);
        case EngineType::Everything: return std::make_unique<EverythingEngine>(cfg);
        case EngineType::Mft:        return std::make_unique<MftEngine>(cfg);
        case EngineType::Walk:
        default:                     return std::make_unique<WalkEngine>();
    }
}

} // namespace ac
