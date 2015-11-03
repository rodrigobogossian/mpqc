//
// Created by Chong Peng on 10/19/15.
//

#include <catch.hpp>
#include "../expression/formula.h"

using namespace mpqc;

TEST_CASE("Formula Expression", "[formula]"){

    SECTION("one body test case"){

        Formula overlap(L"<κ|λ>");
        REQUIRE(overlap.operation().get_operation() == Operation::Operations::Overlap);
        REQUIRE(overlap.left_index().size() == 1);
        REQUIRE(overlap.right_index().size() == 1);
        REQUIRE(overlap.right_index()[0].index() == OrbitalIndex::Index::obs);
        REQUIRE(overlap.left_index()[0].index() == OrbitalIndex::Index::obs);
        REQUIRE(overlap.operation().is_onebody() == true);
        REQUIRE(overlap.notation() == Formula::Notation::Physical);

        Formula kinetic(L"(p q|T|r s)");
        REQUIRE(kinetic.operation().get_operation() == Operation::Operations::Kinetic);
        REQUIRE(kinetic.notation() == Formula::Notation::Chemical);

        Formula r2(L"<p q| R2 |r s>");
        REQUIRE(r2.operation().get_operation() == Operation::Operations::cGTG2);
    }

    SECTION("two body test case"){
        Formula couloumb(L"<p q1|R|a' A1'>");
        REQUIRE(couloumb.left_index()[0].index() == OrbitalIndex::Index::any);
        REQUIRE(couloumb.left_index()[1].index() == OrbitalIndex::Index::any);
        REQUIRE(couloumb.right_index()[0].index() == OrbitalIndex::Index::othervirt);
        REQUIRE(couloumb.right_index()[1].index() == OrbitalIndex::Index::allvirt);
        REQUIRE(couloumb.operation().is_twobody() == true);

        Formula two_center(L"(  |R|a' A1')");
        REQUIRE(two_center.left_index().size() == 0);

        Formula three_center(L"( Κ|R|κ λ1)");
        REQUIRE(three_center.left_index().size() == 1);
        REQUIRE(three_center.right_index().size() == 2);
    }

    SECTION("error handling"){
        // wrong operation
        REQUIRE_THROWS(Formula kinetic(L"<p q|t|r s>"));

        // wrong format
        REQUIRE_THROWS(Formula kinetic(L"p q|T|r s>"));
        REQUIRE_THROWS(Formula kinetic(L"<p |q|K|r s>"));

        REQUIRE_THROWS(Formula kinetic(L"<pq|T|r s>"));
        REQUIRE_THROWS(Formula kinetic(L"<p1q2|T|r s>"));
        REQUIRE_THROWS(Formula kinetic(L"<a'q2|T|r s>"));

    }

}
