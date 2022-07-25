#include <iomanip>
#include "../bvm2.h"

int g_failedTests = 0;

class DocProcessor: public beam::bvm2::ProcessorManager {
public:
    void SelectContext(bool bDependent, uint32_t nChargeNeeded) override {
    }

    void TestQuotedText(const char* what, const std::string& expected) {
        std::ostringstream stream;
        m_pOut = &stream;
        DocQuotedText(what);

        const auto result = stream.str();
        if (stream.str() != expected) {
            if (g_failedTests) std::cout << std::endl;
            std::cout << "TestQuotedText failed: "
                      << "\n\tinput: " << what
                      << "\n\tresult: " << result
                      << "\n\texpected: " << expected;
            g_failedTests++;
        }
    }
};

int main()
{
    DocProcessor processor;
    processor.TestQuotedText("hello", "\"hello\"");
    processor.TestQuotedText("\"", R"("\"")");
    processor.TestQuotedText("\\", R"("\\")");

    for(char ch = 1; ch <= 0x1F; ++ch) {
        char input[2] = {ch, 0};
        std::ostringstream expect;
        expect << "\"\\u" << std::setfill('0') << std::setw(4) << std::hex << static_cast<int>(ch) << "\"";
        processor.TestQuotedText(input, expect.str());
    }

    return g_failedTests ? -1 : 0;
}
