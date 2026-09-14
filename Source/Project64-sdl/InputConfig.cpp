// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <yaml-cpp/yaml.h>

InputConfig::InputConfig()
{
    DefaultBindings(m_Bindings);
}

InputConfig & InputConfig::Get()
{
    static InputConfig Instance;
    return Instance;
}

const std::vector<Binding> & InputConfig::Bindings(N64Control Control) const
{
    return m_Bindings[(int)Control];
}

bool InputConfig::Load(const char * Path)
{
    YAML::Node Root = YAML::LoadFile(Path);
    return Root.IsDefined();
}

void InputConfig::DefaultBindings(std::vector<Binding> * Out)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        Out[i].clear();
    }
}
