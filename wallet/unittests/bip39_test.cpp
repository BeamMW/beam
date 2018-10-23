#include "core/uintBig.h"
#include "core/ecc.h"
#include "core/ecc_native.h"

#include "wallet/mnemonic.h"
#include "utility/helpers.h"

#include "test_helpers.h"

#include <array>
#include <vector>
#include <assert.h>

#include <boost/algorithm/string.hpp>

using namespace beam;
using namespace ECC;

WALLET_TEST_INIT

namespace
{
    struct MnemonicData
    {
        std::string entropy;
        std::string mnemonic;
        std::string seed;
    };

    typedef std::vector<MnemonicData> MnemonicDataList;

    const MnemonicDataList mnemonicTestData = 
    {
        {
            {
                "00000000000000000000000000000000",
                "abandon,abandon,abandon,abandon,abandon,abandon,abandon,abandon,abandon,abandon,abandon,about",
                "5eb00bbddcf069084889a8ab9155568165f5c453ccb85e70811aaed6f6da5fc19a5ac40b389cd370d086206dec8aa6c43daea6690f20ad3d8d48b2d2ce9e38e4"
            },
            {
                "7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f",
                "legal,winner,thank,year,wave,sausage,worth,useful,legal,winner,thank,yellow",
                "878386efb78845b3355bd15ea4d39ef97d179cb712b77d5c12b6be415fffeffe5f377ba02bf3f8544ab800b955e51fbff09828f682052a20faa6addbbddfb096"
            },
            {
                "80808080808080808080808080808080",
                "letter,advice,cage,absurd,amount,doctor,acoustic,avoid,letter,advice,cage,above",
                "77d6be9708c8218738934f84bbbb78a2e048ca007746cb764f0673e4b1812d176bbb173e1a291f31cf633f1d0bad7d3cf071c30e98cd0688b5bcce65ecaceb36"
            },
            {
                "ffffffffffffffffffffffffffffffff",
                "zoo,zoo,zoo,zoo,zoo,zoo,zoo,zoo,zoo,zoo,zoo,wrong",
                "b6a6d8921942dd9806607ebc2750416b289adea669198769f2e15ed926c3aa92bf88ece232317b4ea463e84b0fcd3b53577812ee449ccc448eb45e6f544e25b6"
            },
            {
                "77c2b00716cec7213839159e404db50d",
                "jelly,better,achieve,collect,unaware,mountain,thought,cargo,oxygen,act,hood,bridge",
                "c7b8fbb38c1abe38dfc0fea9797804558dfac244cd7737ae3a1b619991e0ad520155d982f906629639dc39e440520f98f820bea4f886a63a45923a63441f25ef"
            },
            {
                "0460ef47585604c5660618db2e6a7e7f",
                "afford,alter,spike,radar,gate,glance,object,seek,swamp,infant,panel,yellow",
                "3ddfd060236156416f8915ed6ced01c3316292aec7250434f7e32cda2338e76399874787257acad15618c81bcddd88714f8c0d316140dad809f0ca8b1a971679"
            },
            {
                "eaebabb2383351fd31d703840b32e9e2",
                "turtle,front,uncle,idea,crush,write,shrug,there,lottery,flower,risk,shell",
                "4ef6e8484a846392f996b15283906b73be4ec100859ce68689d5a0fad7f761745b86d70ea5f5c43e4cc93ce4b82b3d9aeed7f85d503fac00b10ebbc150399100"
            },
            {
                "18ab19a9f54a9274f03e5209a2ac8a91",
                "board,flee,heavy,tunnel,powder,denial,science,ski,answer,betray,cargo,cat",
                "22087755f76d6fb93ddd19e71106d4d4146f48424a241c0eda88787227827166223f61860d53652b635f360b5a37dd26c8aed3fa10b6f8e95be18f1913f4ca88"
            }
        }
    };
}

void testMnemonic()
{
    for (const auto& testData : mnemonicTestData)
    {
        WordList wordList = createMnemonic(from_hex(testData.entropy), language::en);

        WALLET_CHECK(testData.mnemonic == boost::join(wordList, ","));

        uintBig512 hash = decodeMnemonic(wordList);
        std::vector<uint8_t> data(&hash.m_pData[0], &hash.m_pData[0] + uintBig512::nBytes);
        WALLET_CHECK(testData.seed == to_hex(data.data(), data.size()));
    }    
}

int main()
{
    testMnemonic();
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}