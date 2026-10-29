#pragma once

#include <Tempest/ODevice>

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

class World;
class GameSession;
class Serialize;

class WorldStateStorage final {
  public:
    WorldStateStorage()=default;
    WorldStateStorage(World &w);
    WorldStateStorage(const WorldStateStorage&)=delete;
    WorldStateStorage(WorldStateStorage&&)=default;
    WorldStateStorage& operator = (WorldStateStorage&&)=default;

    bool                 isEmpty() const { return storage.empty(); }
    void                 save(Serialize& fout) const;
    void                 load(Serialize& fin);

    bool                 compareName(std::string_view name) const;

    std::string          name;
    std::vector<uint8_t> storage;
    // set when the storage came from a save made with other scripts (see Serialize::setSymbolMap)
    std::shared_ptr<const std::vector<uint32_t>> symMap;
    // the names of npcs and items in it are of another text language: read them from the scripts again
    bool                 refreshText = false;
  };
