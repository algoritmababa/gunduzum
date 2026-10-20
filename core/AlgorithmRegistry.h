#ifndef TURB_ALGORITHMREGISTRY_H
#define TURB_ALGORITHMREGISTRY_H

#include "Algorithm.h"

#include <functional>
#include <string>
#include <vector>

namespace turb {

// Lists every available algorithm. Adding a new algorithm only requires registering
// its factory here; the GUI, CLI and exporters pick it up automatically.
class AlgorithmRegistry {
public:
    struct Entry {
        std::string id;
        std::string name;
        AlgorithmGroup group;
        int priority; // implementation priority / display order
        std::function<AlgorithmPtr()> factory;
    };

    static const AlgorithmRegistry &instance();

    const std::vector<Entry> &entries() const { return m_entries; }
    AlgorithmPtr create(const std::string &id) const;
    const Entry *find(const std::string &id) const;

private:
    AlgorithmRegistry();
    template <typename T> void add(int priority);

    std::vector<Entry> m_entries;
};

} // namespace turb

#endif
