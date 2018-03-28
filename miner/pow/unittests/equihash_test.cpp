#include "../equihash.h"

#define BOOST_TEST_MODULE equihash_test
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE( equihash_test )
{
    pow::Input input{1, 2, 3, 4, 56};

    pow::uint256_t nonce{1, 2, 4};
    auto proof = pow::get_solution(input, nonce);
    std::cout << proof.solution.size() << std::endl;

    BOOST_CHECK(pow::is_valid_proof(input, proof));
}
