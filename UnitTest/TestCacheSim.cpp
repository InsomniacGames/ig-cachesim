#include "gtest/gtest.h"
#include "gtest-all.cc"

#include "CacheSim/CacheSimInternals.h"
extern "C"
{
#include "udis86/udis86.h"
}

namespace
{
  class CacheTest : public ::testing::Test
  {
  public:
    CacheSim::JaguarCacheSim cache;

  public:
    virtual void SetUp() override
    {
      cache.Init();
    }

    virtual void TearDown() override
    {
    }
  }; 
}

TEST_F(CacheTest, BasicHit)
{
  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, 0x12345678abcd, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit, cache.Access(0, 0x12345678abcd, 8, CacheSim::kRead));
}

TEST_F(CacheTest, BasicCodeHit)
{
  EXPECT_EQ(CacheSim::kL2IMiss, cache.Access(0, 0x12345678abcd, 8, CacheSim::kCodeRead));
  EXPECT_EQ(CacheSim::kI1Hit, cache.Access(0, 0x12345678abcd, 8, CacheSim::kCodeRead));
}

TEST_F(CacheTest, BasicAssoc)
{
  uintptr_t la = 0x40;
  uintptr_t lb = la + 512 * 0x40;

  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, lb, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, lb, 8, CacheSim::kRead));
}

TEST_F(CacheTest, CoreInvalidating)
{
  uintptr_t la = 0x40;

  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(1, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(2, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(3, la, 8, CacheSim::kRead));

  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(1, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(2, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(3, la, 8, CacheSim::kRead));

  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kWrite));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(1, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(2, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2Hit,  cache.Access(3, la, 8, CacheSim::kRead));
}

TEST_F(CacheTest, CoreInvalidatingModule)
{
  uintptr_t la = 0x40;

  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kRead));

  // Simulate other package invalidating L2 line
  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(5, la, 8, CacheSim::kWrite));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(5, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, la, 8, CacheSim::kRead));
  EXPECT_EQ(CacheSim::kD1Hit,  cache.Access(0, la, 8, CacheSim::kRead));
}

TEST_F(CacheTest, FullAssoc)
{
  uintptr_t base = 0x40;
  uintptr_t multiplier = 0x40 * 512;  // Make sure it hits the same way in L1

  EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, base, 8, CacheSim::kRead));

  for (int i = 1; i <= 8; ++i)
  {
    EXPECT_EQ(CacheSim::kL2DMiss, cache.Access(0, base + i * multiplier, 8, CacheSim::kRead));
  }

  for (int i = 1; i <= 8; ++i)
  {
    EXPECT_EQ(CacheSim::kD1Hit, cache.Access(0, base + i * multiplier, 8, CacheSim::kRead));
  }

  EXPECT_EQ(CacheSim::kL2Hit, cache.Access(0, base, 8, CacheSim::kRead));
}

TEST(Disassembler, Movhps)
{
  static const uint8_t insn[] = { 0x0f, 0x16, 0x0f };
  struct ud ud;
  ud_init(&ud);
  ud_set_mode(&ud, 64);
  ud_set_input_buffer(&ud, (const uint8_t*)insn, 16);
  int ilen = ud_disassemble(&ud);
  ASSERT_EQ(3, ilen);

  ASSERT_EQ(UD_Imovhps, ud.mnemonic);
  ASSERT_EQ(UD_OP_REG, ud.operand[0].type);
  ASSERT_EQ(UD_R_XMM1, ud.operand[0].base);
  ASSERT_EQ(128, ud.operand[0].size);

  ASSERT_EQ(UD_OP_MEM, ud.operand[1].type);
  ASSERT_EQ(UD_R_RDI, ud.operand[1].base);
  ASSERT_EQ(64, ud.operand[1].size);
}

#if 0
TEST(RunTheThing, Minimal)
{
  CacheSimInit();
  CacheSimSetThreadCoreMapping(GetCurrentThreadId(), 0);
  CacheSimStartCapture();

  static volatile int a;
  
  a = 1;

  CacheSimEndCapture(true);
}
#endif

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
