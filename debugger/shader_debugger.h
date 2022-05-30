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
#pragma once

#include "bvm/bvm2.h"
#include "utility/common.h"
#include <boost/optional.hpp>
#include <unordered_set>
#include <unordered_map>

#ifdef small
#undef small
#endif // small
#include "3rdparty/libelfin/dwarf/dwarf++.hh"

namespace beam
{
    namespace Wasm { struct Processor; }
    class ShaderDebugger
    {
    public:

        enum class Event
        {
            BreakpointHit,
            Stepped,
            Paused,
            Output
        };

        struct Variable
        {
            std::string name;
            std::string type;
            std::string value;
        };

        struct Frame
        {
            bool m_HasInfo = false;
            std::string m_Name;
            int64_t m_Line = 1;
            int64_t m_Column = 1;
            std::string m_FilePath;
            dwarf::taddr m_FrameBase;
            dwarf::taddr m_Address;
            int64_t m_ID;
            dwarf::die m_Die;
            std::vector<Variable> m_Parameters;
        };

        enum class Action
        {
            NoAction,
            Continue,
            Pause,
            StepIn,
            SteppingIn,
            StepOut,
            SteppingOut,
            StepOver,
            SteppingOver
        };

        using EventHandler = std::function<void(Event, const std::string&)>;

        ShaderDebugger(const std::string& shaderPath, const EventHandler& onEvent);
        void DoDebug(const Wasm::Processor& proc);

        //
        // Actions
        // 

        // Instructs the debugger to continue execution.
        void Run();

        // Instructs the debugger to Pause execution.
        void Pause();

        // Instructs the debugger to step forward one line.
        void StepForward();

        void StepIn();

        void StepOut();

        //
        // Breakpoints
        //

        // Clears all set breakpoints for given source file.
        void ClearBreakpoints(const std::string& path);

        // Sets a new breakpoint on the given line.
        std::pair<int32_t, bool> AddBreakpoint(const std::string& filePath, int64_t line);
        bool CanSetBreakpoint(const std::string& filePath, int64_t line);

        //
        // State
        //
        std::vector<Frame> GetCallStack() const;
        //std::string GetStackTraceName(const Frame & frame, const dap::optional<dap::StackFrameFormat>&format);
        int64_t GetVariableReferenceID(int64_t frameID) const;
        boost::optional<std::vector<Variable>> GetVariables(int64_t id, bool hex) const;

    private:
        bool ContainsPC(const dwarf::die& d, dwarf::taddr pc) const;
        bool FindFunctionByPC2(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack);
        bool FindFunctionByPC(const dwarf::die& d, dwarf::taddr pc, std::vector<dwarf::die>& stack);
        std::string DumpDIE(const dwarf::die& node, int intent = 0);
        const Frame* FindFrameByID(int64_t frameID) const;
        std::vector<Variable> LoadVariables(const Frame& frame, bool hex) const;
        std::string GetTypeName(const dwarf::die& d) const;

        struct FunctionInfo
        {
            std::vector<dwarf::die> m_Dies;
            dwarf::line_table::iterator m_Line;
        };

        boost::optional<FunctionInfo> FindFunctionByPC(dwarf::taddr pc);

        bool LookupSourceLine(dwarf::taddr pc, std::vector<dwarf::die>& stack);
        std::vector<Variable> GetFormalParameters(const dwarf::die& d);
        void ProcessAction(size_t stackHeight);
        void EmitEvent(Event event = Event::Stepped);
        void DoAction(Action action);
    protected:
        std::string m_ShaderName;
        ByteBuffer m_Buffer;
        Wasm::Compiler m_Compiler;
        std::unique_ptr<dwarf::dwarf> m_DebugInfo;
        std::shared_ptr<dwarf::line_table::iterator> m_CurrentLine;
        std::shared_ptr<dwarf::line_table::iterator> m_PrevLine;
        EventHandler m_OnEvent;
        mutable  std::mutex m_Mutex;
        const Wasm::Processor* m_Processor = nullptr;
        Action m_NextAction = Action::Pause;
        std::condition_variable m_BvmEvent;
        std::condition_variable m_DapEvent;
        std::vector<Frame> m_CallStack;
        size_t m_StackHeight = 0;
        std::unordered_map<std::string, std::unordered_set<int64_t>> m_Breakpoints;
    };
}
