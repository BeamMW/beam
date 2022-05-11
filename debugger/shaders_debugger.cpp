// Copyright 2018-2022 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "shader_debugger.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include <iomanip>

using namespace beam;
namespace fs = boost::filesystem;

namespace
{
    std::string FixPath(const std::string& p)
    {
        std::string res = fs::system_complete(p).make_preferred().string();
#ifdef OS_WINDOWS
        std::transform(begin(res), end(res), begin(res), [](unsigned char c) { return std::tolower(c); });
#endif // OS_WINDOWS

        return res;
    }

    class WasmLoader : public dwarf::loader
    {
    public:
        WasmLoader(const beam::Wasm::Compiler& c) : m_Compiler(c)
        {
        }

        const void* load(dwarf::section_type section, size_t* size_out)
        {
            const auto& name = dwarf::elf::section_type_to_name(section);
            const auto& sections = m_Compiler.m_CustomSections;
            auto it = std::find_if(sections.begin(), sections.end(), [&](const auto& s) { return s.m_Name == name; });

            if (it == sections.end())
                return nullptr;

            *size_out = it->size();
            return it->data();
        }

    private:
        beam::Wasm::Compiler m_Compiler;
    };

    ByteBuffer LoadShader(const std::string& sz)
    {
        std::FStream fs;
        fs.Open(sz.data(), true, true);

        ByteBuffer res;
        res.resize(static_cast<size_t>(fs.get_Remaining()));
        if (!res.empty())
        {
            fs.read(&res.front(), res.size());
        }

        return res;
    }
} // namespace

namespace beam
{
    ShaderDebugger::ShaderDebugger(const std::string& shaderPath, EventHandler&& onEvent)
        : m_ShaderName(fs::path(shaderPath).filename().string())
        , m_OnEvent(std::move(onEvent))
    {
        // load symbols
        auto buffer = LoadShader(shaderPath);
        ByteBuffer byteCode;
        bvm2::Processor::Compile(m_Compiler, byteCode, buffer, bvm2::Processor::Kind::Contract);
        m_DebugInfo = std::make_unique<dwarf::dwarf>(std::make_shared<WasmLoader>(m_Compiler));
    }

    void ShaderDebugger::DoDebug(const Wasm::Processor& proc)
    {
        using namespace dwarf;
        std::vector<die> stack;
        auto ip = proc.get_Ip();
        auto it = m_Compiler.m_IpMap.find(ip);
        if (it == m_Compiler.m_IpMap.end())
        {
            return;
        }
        auto myIp = it->second;

        std::unique_lock lock(m_Mutex);

        if (LookupSourceLine(myIp, stack))
        {
            // Update current state
            auto stackHeight = m_CallStack.size();
            m_CallStack.clear();
            std::vector<taddr> callStack;
            callStack.reserve(proc.m_CallStack.size());
            for (auto p : proc.m_CallStack)
            {
                callStack.push_back(proc.m_Stack.m_pPtr[p]);
            }

            callStack.push_back(ip);
            for (auto addr : callStack)
            {
                auto it2 = m_Compiler.m_IpMap.find(addr);
                if (it2 != m_Compiler.m_IpMap.end())
                {
                    if (auto funcInfo = FindFunctionByPC(it2->second); funcInfo)
                    {
                        const auto& c = funcInfo->m_Line;
                        uint64_t line = c->line;
                        uint64_t column = c->column;
                        std::string filePath = c->file->path;
                        m_CallStack.resize(m_CallStack.size() + funcInfo->m_Dies.size());
                        size_t i = m_CallStack.size() - 1;
                        for (const auto& die : funcInfo->m_Dies)
                        {
                            auto& frame = m_CallStack[i--];
                            auto addressPos = (i < proc.m_CallStack.size()) ? proc.m_CallStack[i] : 0;
                            if (proc.m_Stack.m_Pos <= addressPos)
                            {
                                return;
                            }
                            auto framePos = proc.m_Stack.m_pPtr[addressPos + 1];
                            if ((framePos & Wasm::MemoryType::Mask) != Wasm::MemoryType::Stack)
                            {
                                return;
                            }
                            framePos &= ~Wasm::MemoryType::Mask;
                            auto frameBase = reinterpret_cast<uint8_t*>(proc.m_Stack.m_pPtr) + framePos;
                            frame.m_Address = addr;
                            frame.m_Die = die;
                            auto d = die;
                            if (d.tag == DW_TAG::subprogram &&
                                d.has(DW_AT::specification))
                            {
                                d = at_specification(d);
                            }
                            else if (d.tag == DW_TAG::inlined_subroutine)
                            {
                                d = at_abstract_origin(d);
                            }

                            if (d.has(DW_AT::name))
                            {
                                frame.m_HasInfo = true;
                                frame.m_Name = at_name(d);
                                frame.m_FilePath = FixPath(filePath);
                                frame.m_Line = line;
                                frame.m_Column = column;
                                frame.m_ID = it2->second;
                                frame.m_Parameters = GetFormalParameters(d);
                                frame.m_FrameBase = reinterpret_cast<taddr>(frameBase);
                            }
                            else
                            {
                                frame.m_Name = "[External Code]";
                            }
                            if (die.tag == DW_TAG::inlined_subroutine)
                            {
                                if (die.has(DW_AT::call_file))
                                {
                                    auto fileIndex = (unsigned int)die[DW_AT::call_file].as_uconstant();
                                    const auto& lineTable = dynamic_cast<const compilation_unit&>(die.get_unit()).get_line_table();
                                    filePath = lineTable.get_file(fileIndex)->path;
                                }
                                if (die.has(DW_AT::call_line))
                                    line = die[DW_AT::call_line].as_uconstant();
                                if (die.has(DW_AT::call_column))
                                    column = die[DW_AT::call_column].as_uconstant();
                            }
                        }
                        continue;
                    }
                }
                std::stringstream ss;
                ss << std::hex << std::setw(8) << std::setfill('0') << addr << "()";
                auto& frame = m_CallStack.emplace_back();
                frame.m_Name = ss.str();
                frame.m_Address = addr;
            }
            m_Processor = &proc;
            // Process user's action
            ProcessAction(stackHeight);
            // Wait for the next user's action
            m_DapEvent.wait(lock, [this] {return m_NextAction != Action::NoAction; });
            // Continue execution
        }
    }

    void ShaderDebugger::Run()
    {
        DoAction(Action::Continue);
    }

    void ShaderDebugger::Pause()
    {
        DoAction(Action::Pause);
    }

    void ShaderDebugger::StepForward()
    {
        DoAction(Action::StepOver);
    }

    void ShaderDebugger::StepIn()
    {
        DoAction(Action::StepIn);
    }

    void ShaderDebugger::StepOut()
    {
        DoAction(Action::StepOut);
    }

    void ShaderDebugger::ClearBreakpoints(const std::string& path)
    {
        std::unique_lock lock(m_Mutex);
        auto it = m_Breakpoints.find(path);
        if (it != m_Breakpoints.end())
        {
            it->second.clear();
        }
    }

    std::pair<int32_t, bool> ShaderDebugger::AddBreakpoint(const std::string& filePath, int64_t line)
    {
        std::unique_lock lock(m_Mutex);
        std::string filePathC = FixPath(filePath);
        m_Breakpoints[filePathC].emplace(line);
        size_t id = std::hash<std::string>{}(filePathC);
        boost::hash_combine(id, line);
        return { static_cast<int32_t>(id), CanSetBreakpoint(filePathC, line) };
    }

    bool ShaderDebugger::CanSetBreakpoint(const std::string& filePath, int64_t line)
    {
        fs::path path(filePath);
        // TODO: avoid linear search
        for (const auto& cu : m_DebugInfo->compilation_units())
        {
            const auto& lineTable = cu.get_line_table();
            static_assert(std::is_same_v<std::iterator_traits<dwarf::line_table::iterator>::iterator_category, std::forward_iterator_tag> == true);
            auto it = std::find_if(lineTable.begin(), lineTable.end(),
                [&](const auto& entry)
                {
                    return entry.line == line && entry.is_stmt && fs::equivalent(path, fs::path(entry.file->path));
                });
            if (it != lineTable.end())
            {
                return true;
            }
        }
        return false;
    }

    std::vector<ShaderDebugger::Frame> ShaderDebugger::GetCallStack() const
    {
        std::unique_lock lock(m_Mutex);
        return m_CallStack;
    }

    int64_t ShaderDebugger::GetVariableReferenceID(int64_t frameID) const
    {
        std::unique_lock lock(m_Mutex);
        auto f = FindFrameByID(frameID);
        if (f)
        {
            return f->m_ID;
        }
        return -1;
    }

    boost::optional<std::vector<ShaderDebugger::Variable>> ShaderDebugger::GetVariables(int64_t id, bool hex) const
    {
        std::unique_lock lock(m_Mutex);
        auto f = FindFrameByID(id);
        if (!f)
        {
            return {};
        }
        return LoadVariables(*f, hex);
    }

    bool ShaderDebugger::ContainsPC(const dwarf::die& d, dwarf::taddr pc) const
    {
        using namespace dwarf;
        return (d.has(DW_AT::low_pc) || d.has(DW_AT::ranges)) && die_pc_range(d).contains(pc);
    }

    bool ShaderDebugger::FindFunctionByPC2(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack)
    {
        using namespace dwarf;

        // Scan children first to find most specific DIE
        bool found = false;
        for (auto& child : d)
        {
            found = FindFunctionByPC2(child, pc, stack);
            if (found)
            {
                auto s = DumpDIE(child);
                s;
                break;
            }
        }
        switch (d.tag)
        {
        case DW_TAG::subprogram:
            try
            {
                if (ContainsPC(d, pc))
                {
                    found = true;
                    stack.push_back(d);
                }
            }
            catch (const std::out_of_range&)
            {
            }
            catch (const value_type_mismatch&)
            {
            }
            break;
        case DW_TAG::inlined_subroutine:
            try
            {
                if (ContainsPC(d, pc))
                {
                    if ((found && stack.back().tag == DW_TAG::inlined_subroutine) || !found)
                    {
                        found = true;
                        stack.push_back(d);
                    }
                }
            }
            catch (const std::out_of_range&)
            {
            }
            catch (const value_type_mismatch&)
            {
            }
            break;
        default:
            break;
        }
        return found;
    }

    bool ShaderDebugger::FindFunctionByPC(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack)
    {
        using namespace dwarf;

        // Scan children first to find most specific DIE
        bool found = false;
        for (auto& child : d)
        {
            found = FindFunctionByPC(child, pc, stack);
            if (found)
            {
                auto s = DumpDIE(child);
                s;
                break;
            }
        }
        switch (d.tag)
        {
        case DW_TAG::subprogram:
        case DW_TAG::inlined_subroutine:
            try
            {
                if (found || ContainsPC(d, pc))
                {
                    found = true;
                    stack.push_back(d);
                }
            }
            catch (const std::out_of_range&)
            {
            }
            catch (const value_type_mismatch&)
            {
            }
            break;
        default:
            break;
        }
        return found;
    }

    std::string ShaderDebugger::DumpDIE(const dwarf::die& node, int intent)
    {
        std::stringstream ss;
        for (int i = 0; i < intent; ++i)
            ss << "      ";
        ss << "<" << node.get_section_offset() << "> " << to_string(node.tag) << '\n';

        for (auto& attr : node.attributes())
        {
            for (int i = 0; i < intent; ++i)
                ss << "      ";

            ss << "      " << to_string(attr.first) << " " << to_string(attr.second) << '\n';
        }

        for (const auto& child : node)
            ss << DumpDIE(child, intent + 1);
        return ss.str();
    }

    const ShaderDebugger::Frame* ShaderDebugger::FindFrameByID(int64_t frameID) const
    {
        for (const auto& frame : m_CallStack)
        {
            if (frameID == frame.m_ID)
            {
                return &frame;
            }
        }
        return nullptr;
    }

    struct MyContext : dwarf::expr_context
    {
        dwarf::taddr m_fb;
        MyContext(dwarf::taddr fb)
            : m_fb(fb)
        {}
        dwarf::taddr fbreg() override
        {
            return m_fb;
        }
        dwarf::taddr reg(unsigned regnum) override
        {
            return 0;
        }
    };

    std::vector<ShaderDebugger::Variable> ShaderDebugger::LoadVariables(const Frame& frame, bool hex) const
    {
        std::vector<Variable> res;
        using namespace dwarf;
        switch (frame.m_Die.tag)
        {
        case DW_TAG::subprogram:
        case DW_TAG::inlined_subroutine:
            for (const auto& c : frame.m_Die)
            {
                if (c.tag == DW_TAG::formal_parameter ||
                    c.tag == DW_TAG::variable)
                {
                    auto& v = res.emplace_back();
                    try
                    {
                        if (c.has(DW_AT::name))
                        {
                            v.name = at_name(c);
                        }
                        auto type = at_type(c);
                        v.type = GetTypeName(type);
                        v.value = "unknown";
                        if (c.has(DW_AT::location))
                        {
                            auto loc = c[DW_AT::location];
                            if (loc.get_type() == value::type::exprloc)
                            {
                                auto expr = loc.as_exprloc();
                                MyContext ctx(frame.m_FrameBase);
                                auto val = expr.evaluate(&ctx, 0);
                                if (type.has(DW_AT::byte_size) && type.has(DW_AT::encoding))
                                {
                                    auto size = at_byte_size(type, &ctx);
                                    size;
                                }

                                std::stringstream ss;
                                if (hex)
                                {
                                    ss << "0x" << std::hex << std::setw(8) << std::setfill('0');
                                }
                                ss << Wasm::from_wasm(*reinterpret_cast<int32_t*>(val.value));
                                v.value = ss.str();
                            }

                        }
                    }
                    catch (...) {}
                }
            }
            break;
        default:
            break;
        }
        return res;
    }

    std::string ShaderDebugger::GetTypeName(const dwarf::die& d) const
    {
        using namespace dwarf;
        try
        {
            switch (d.tag)
            {
            case DW_TAG::const_type:
                return "const " + GetTypeName(at_type(d));
            case DW_TAG::pointer_type:
                return GetTypeName(at_type(d)) + "*";
            case DW_TAG::reference_type:
                return GetTypeName(at_type(d)) + "&";
            case DW_TAG::rvalue_reference_type:
                return GetTypeName(at_type(d)) + "&&";
            default:
                return at_name(d);
            }
        }
        catch (...)
        {

        }
        return {};
    }

    boost::optional<ShaderDebugger::FunctionInfo> ShaderDebugger::FindFunctionByPC(dwarf::taddr pc)
    {
        for (auto& cu : m_DebugInfo->compilation_units())
        {
            if (ContainsPC(cu.root(), pc))
            {
                // Map PC to a line
                auto& lt = cu.get_line_table();
                auto it = lt.find_address(pc);
                if (it == lt.end())
                {
                    return {};
                }

                std::vector<dwarf::die> stack;
                if (FindFunctionByPC2(cu.root(), pc, stack))
                {
                    return FunctionInfo{ std::move(stack), it };
                }
                break;
            }
        }
        return {};
    }

    bool ShaderDebugger::LookupSourceLine(dwarf::taddr pc, std::vector<dwarf::die>& stack)
    {
        for (auto& cu : m_DebugInfo->compilation_units())
        {
            if (ContainsPC(cu.root(), pc))
            {
                // Map PC to a line
                auto& lt = cu.get_line_table();
                auto it = lt.find_address(pc);
                if (it == lt.end())
                    return false;

                if (m_CurrentLine)
                {
                    if (*m_CurrentLine == it)
                        return false;

                    m_PrevLine = m_CurrentLine;
                }
                m_CurrentLine = std::make_shared<dwarf::line_table::iterator>(it);

                // Map PC to an object
                // XXX Index/helper/something for looking up PCs
                // XXX DW_AT_specification and DW_AT_abstract_origin
                if (FindFunctionByPC(cu.root(), pc, stack))
                {
                    for (auto& d : stack)
                    {
                        DumpDIE(d);
                    }
                    return true;
                }
                break;
            }
        }
        return false;
    }

    std::vector<ShaderDebugger::Variable> ShaderDebugger::GetFormalParameters(const dwarf::die& d)
    {
        std::vector<Variable> res;
        using namespace dwarf;
        switch (d.tag)
        {
        case DW_TAG::subprogram:
        case DW_TAG::inlined_subroutine:
            for (const auto& c : d)
            {
                if (c.tag == DW_TAG::formal_parameter)
                {
                    if (c.has(DW_AT::artificial) && at_artificial(c) == true)
                    {
                        continue;
                    }
                    auto& v = res.emplace_back();
                    try
                    {
                        if (c.has(DW_AT::name))
                        {
                            v.name = at_name(c);
                        }
                        v.type = GetTypeName(at_type(c));
                    }
                    catch (...) {}
                }
            }
            break;
        default:
            break;
        }
        return res;
    }

    void ShaderDebugger::ProcessAction(size_t stackHeight)
    {
        auto& c = *m_CurrentLine;
        switch (m_NextAction)
        {
        case Action::Continue:

            if (const auto& call = m_CallStack.back();
                c->is_stmt && m_Breakpoints[call.m_FilePath].count(call.m_Line))
            {
                EmitEvent(Event::BreakpointHit);
            }
            break;
        case Action::Pause:
            if (c->is_stmt)
            {
                EmitEvent(Event::Paused);
            }
            break;
        case Action::StepOver:

            m_StackHeight = stackHeight;
            m_NextAction = Action::SteppingOver;
            break;
        case Action::SteppingOver:

            if (c->is_stmt && m_CallStack.size() <= m_StackHeight)
            {
                EmitEvent(Event::Stepped);
            }
            break;
        case Action::StepIn:

            m_NextAction = Action::SteppingIn;
            break;
        case Action::SteppingIn:

            EmitEvent(Event::Stepped);
            break;
        case Action::StepOut:

            if (!m_CallStack.empty())
            {
                m_StackHeight = stackHeight - 1;
                m_NextAction = Action::SteppingOut;
            }
            break;
        case Action::SteppingOut:

            if (m_StackHeight == m_CallStack.size())
            {
                EmitEvent(Event::Stepped);
            }
            break;
        case Action::NoAction:
            break;
        }
    }

    void ShaderDebugger::EmitEvent(Event event)
    {
        m_NextAction = Action::NoAction;
        m_OnEvent(event, "");
    }

    void ShaderDebugger::DoAction(Action action)
    {
        std::unique_lock lock(m_Mutex);
        m_NextAction = action;
        m_DapEvent.notify_one();
    }

} // namespace beam
