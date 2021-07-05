#include "gtest/gtest.h"
#include "goalc/emitter/CodeTester.h"
#include "goalc/emitter/IGen.h"

using namespace emitter;

TEST(EmitterAVX, VF_NOP) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::nop_vf());
  EXPECT_EQ(tester.dump_to_hex_string(true), "D9D0");
}

TEST(EmitterAVX, WAIT_VF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::wait_vf());
  EXPECT_EQ(tester.dump_to_hex_string(true), "9B");
}

TEST(EmitterAVX, MOV_VF) {
  CodeTester tester;
  tester.init_code_buffer(10000);
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      tester.emit(IGen::mov_vf_vf(XMM0 + i, XMM0 + j));
    }
  }

  EXPECT_EQ(
      tester.dump_to_hex_string(true),
      "C5F828C0C5F828C1C5F828C2C5F828C3C5F828C4C5F828C5C5F828C6C5F828C7C57829C0C57829C8C57829D0C578"
      "29D8C57829E0C57829E8C57829F0C57829F8C5F828C8C5F828C9C5F828CAC5F828CBC5F828CCC5F828CDC5F828CE"
      "C5F828CFC57829C1C57829C9C57829D1C57829D9C57829E1C57829E9C57829F1C57829F9C5F828D0C5F828D1C5F8"
      "28D2C5F828D3C5F828D4C5F828D5C5F828D6C5F828D7C57829C2C57829CAC57829D2C57829DAC57829E2C57829EA"
      "C57829F2C57829FAC5F828D8C5F828D9C5F828DAC5F828DBC5F828DCC5F828DDC5F828DEC5F828DFC57829C3C578"
      "29CBC57829D3C57829DBC57829E3C57829EBC57829F3C57829FBC5F828E0C5F828E1C5F828E2C5F828E3C5F828E4"
      "C5F828E5C5F828E6C5F828E7C57829C4C57829CCC57829D4C57829DCC57829E4C57829ECC57829F4C57829FCC5F8"
      "28E8C5F828E9C5F828EAC5F828EBC5F828ECC5F828EDC5F828EEC5F828EFC57829C5C57829CDC57829D5C57829DD"
      "C57829E5C57829EDC57829F5C57829FDC5F828F0C5F828F1C5F828F2C5F828F3C5F828F4C5F828F5C5F828F6C5F8"
      "28F7C57829C6C57829CEC57829D6C57829DEC57829E6C57829EEC57829F6C57829FEC5F828F8C5F828F9C5F828FA"
      "C5F828FBC5F828FCC5F828FDC5F828FEC5F828FFC57829C7C57829CFC57829D7C57829DFC57829E7C57829EFC578"
      "29F7C57829FFC57828C0C57828C1C57828C2C57828C3C57828C4C57828C5C57828C6C57828C7C4417828C0C44178"
      "28C1C4417828C2C4417828C3C4417828C4C4417828C5C4417828C6C4417828C7C57828C8C57828C9C57828CAC578"
      "28CBC57828CCC57828CDC57828CEC57828CFC4417828C8C4417828C9C4417828CAC4417828CBC4417828CCC44178"
      "28CDC4417828CEC4417828CFC57828D0C57828D1C57828D2C57828D3C57828D4C57828D5C57828D6C57828D7C441"
      "7828D0C4417828D1C4417828D2C4417828D3C4417828D4C4417828D5C4417828D6C4417828D7C57828D8C57828D9"
      "C57828DAC57828DBC57828DCC57828DDC57828DEC57828DFC4417828D8C4417828D9C4417828DAC4417828DBC441"
      "7828DCC4417828DDC4417828DEC4417828DFC57828E0C57828E1C57828E2C57828E3C57828E4C57828E5C57828E6"
      "C57828E7C4417828E0C4417828E1C4417828E2C4417828E3C4417828E4C4417828E5C4417828E6C4417828E7C578"
      "28E8C57828E9C57828EAC57828EBC57828ECC57828EDC57828EEC57828EFC4417828E8C4417828E9C4417828EAC4"
      "417828EBC4417828ECC4417828EDC4417828EEC4417828EFC57828F0C57828F1C57828F2C57828F3C57828F4C578"
      "28F5C57828F6C57828F7C4417828F0C4417828F1C4417828F2C4417828F3C4417828F4C4417828F5C4417828F6C4"
      "417828F7C57828F8C57828F9C57828FAC57828FBC57828FCC57828FDC57828FEC57828FFC4417828F8C4417828F9"
      "C4417828FAC4417828FBC4417828FCC4417828FDC4417828FEC4417828FF");
}

TEST(EmitterAVX, LoadVF_Reg) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::loadvf_gpr64_plus_gpr64(XMM0 + 3, RSI, R15));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64(XMM0 + 3, R12, R15));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64(XMM0 + 13, RSI, R15));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64(XMM0 + 13, R12, R15));

  EXPECT_EQ(tester.dump_to_hex_string(true), "C4C178281C37C48178281C3CC44178282C37C40178282C3C");
}

TEST(EmitterAVX, LoadVF_RegS8) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s8(XMM0 + 3, RSI, R15, -3));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s8(XMM0 + 3, R12, R15, -3));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s8(XMM0 + 13, RSI, R15, -3));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s8(XMM0 + 13, R12, R15, -3));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C4C178285C37FDC48178285C3CFDC44178286C37FDC40178286C3CFD");
}

TEST(EmitterAVX, LoadVF_RegS32) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s32(XMM0 + 3, RSI, R15, -0x100));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s32(XMM0 + 3, R12, R15, -0x100));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s32(XMM0 + 13, RSI, R15, -0x100));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64_plus_s32(XMM0 + 13, R12, R15, -0x100));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C4C178289C3700FFFFFFC48178289C3C00FFFFFFC4417828AC3700FFFFFFC4017828AC3C00FFFFFF");
}

TEST(EmitterAVX, StoreVF_Reg) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::storevf_gpr64_plus_gpr64(XMM0 + 3, RSI, R15));
  tester.emit(IGen::storevf_gpr64_plus_gpr64(XMM0 + 3, R12, R15));
  tester.emit(IGen::storevf_gpr64_plus_gpr64(XMM0 + 13, RSI, R15));
  tester.emit(IGen::storevf_gpr64_plus_gpr64(XMM0 + 13, R12, R15));

  EXPECT_EQ(tester.dump_to_hex_string(true), "C4C178291C37C48178291C3CC44178292C37C40178292C3C");
}

TEST(EmitterAVX, StoreVF_RegS8) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s8(XMM0 + 3, RSI, R15, -3));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s8(XMM0 + 3, R12, R15, -3));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s8(XMM0 + 13, RSI, R15, -3));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s8(XMM0 + 13, R12, R15, -3));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C4C178295C37FDC48178295C3CFDC44178296C37FDC40178296C3CFD");
}

TEST(EmitterAVX, StoreVF_RegS32) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s32(XMM0 + 3, RSI, R15, -0x100));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s32(XMM0 + 3, R12, R15, -0x100));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s32(XMM0 + 13, RSI, R15, -0x100));
  tester.emit(IGen::storevf_gpr64_plus_gpr64_plus_s32(XMM0 + 13, R12, R15, -0x100));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C4C178299C3700FFFFFFC48178299C3C00FFFFFFC4417829AC3700FFFFFFC4017829AC3C00FFFFFF");
}

TEST(EmitterAVX, MulVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::mul_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::mul_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::mul_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::mul_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::mul_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::mul_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::mul_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::mul_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E059DBC4C16059DDC59059DBC4C11059DDC56059EBC4416059EDC51059EBC4411059ED");
}

TEST(EmitterAVX, ShuffleVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::shuffle_vf(XMM0 + 3, XMM0 + 4, 1, 3, 2, 1));
  tester.emit(IGen::shuffle_vf(XMM0 + 3, XMM0 + 14, 1, 3, 2, 1));
  tester.emit(IGen::shuffle_vf(XMM0 + 13, XMM0 + 4, 1, 3, 2, 1));
  tester.emit(IGen::shuffle_vf(XMM0 + 13, XMM0 + 14, 1, 3, 2, 1));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5D8C6DC6DC4C108C6DE6DC558C6EC6DC44108C6EE6D");
}

TEST(EmitterAVX, SplatVF_X) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 4, Register::VF_ELEMENT::X));
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 14, Register::VF_ELEMENT::X));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 4, Register::VF_ELEMENT::X));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 14, Register::VF_ELEMENT::X));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5D8C6DC00C4C108C6DE00C558C6EC00C44108C6EE00");
}

TEST(EmitterAVX, SplatVF_Y) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 4, Register::VF_ELEMENT::Y));
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 14, Register::VF_ELEMENT::Y));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 4, Register::VF_ELEMENT::Y));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 14, Register::VF_ELEMENT::Y));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5D8C6DC55C4C108C6DE55C558C6EC55C44108C6EE55");
}

TEST(EmitterAVX, SplatVF_Z) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 4, Register::VF_ELEMENT::Z));
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 14, Register::VF_ELEMENT::Z));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 4, Register::VF_ELEMENT::Z));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 14, Register::VF_ELEMENT::Z));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5D8C6DCAAC4C108C6DEAAC558C6ECAAC44108C6EEAA");
}

TEST(EmitterAVX, SplatVF_W) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 4, Register::VF_ELEMENT::W));
  tester.emit(IGen::splat_vf(XMM0 + 3, XMM0 + 14, Register::VF_ELEMENT::W));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 4, Register::VF_ELEMENT::W));
  tester.emit(IGen::splat_vf(XMM0 + 13, XMM0 + 14, Register::VF_ELEMENT::W));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5D8C6DCFFC4C108C6DEFFC558C6ECFFC44108C6EEFF");
}

TEST(EmitterAVX, XorVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::xor_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::xor_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::xor_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::xor_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::xor_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::xor_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::xor_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::xor_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E057DBC4C16057DDC59057DBC4C11057DDC56057EBC4416057EDC51057EBC4411057ED");
}

TEST(EmitterAVX, SubVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::sub_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::sub_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::sub_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::sub_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::sub_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::sub_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::sub_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::sub_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E05CDBC4C1605CDDC5905CDBC4C1105CDDC5605CEBC441605CEDC5105CEBC441105CED");
}

TEST(EmitterAVX, AddVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::add_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::add_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::add_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::add_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::add_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::add_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::add_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::add_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E058DBC4C16058DDC59058DBC4C11058DDC56058EBC4416058EDC51058EBC4411058ED");
}

TEST(EmitterAVX, MaxVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::max_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::max_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::max_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::max_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::max_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::max_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::max_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::max_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E05FDBC4C1605FDDC5905FDBC4C1105FDDC5605FEBC441605FEDC5105FEBC441105FED");
}

TEST(EmitterAVX, MinVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::min_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::min_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::min_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::min_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::min_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::min_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::min_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::min_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E05DDBC4C1605DDDC5905DDBC4C1105DDDC5605DEBC441605DEDC5105DEBC441105DED");
}

TEST(EmitterAVX, BlendVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::blend_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3, 3));
  tester.emit(IGen::blend_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13, 3));
  tester.emit(IGen::blend_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3, 3));
  tester.emit(IGen::blend_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13, 3));
  tester.emit(IGen::blend_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3, 3));
  tester.emit(IGen::blend_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13, 3));
  tester.emit(IGen::blend_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3, 3));
  tester.emit(IGen::blend_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13, 3));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C4E3610CDB03C4C3610CDD03C4E3110CDB03C4C3110CDD03C463610CEB03C443610CED03C463110CEB03C4"
            "43110CED03");
}

TEST(EmitterAVX, DivVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::div_vf(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::div_vf(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::div_vf(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::div_vf(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::div_vf(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::div_vf(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::div_vf(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::div_vf(XMM0 + 13, XMM0 + 13, XMM0 + 13));

  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E05EDBC4C1605EDDC5905EDBC4C1105EDDC5605EEBC441605EEDC5105EEBC441105EED");
}

TEST(EmitterAVX, SqrtVF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::sqrt_vf(XMM0 + 3, XMM0 + 4));
  tester.emit(IGen::sqrt_vf(XMM0 + 3, XMM0 + 14));
  tester.emit(IGen::sqrt_vf(XMM0 + 13, XMM0 + 4));
  tester.emit(IGen::sqrt_vf(XMM0 + 13, XMM0 + 14));

  EXPECT_EQ(tester.dump_to_hex_string(true), "C5F851DCC4C17851DEC57851ECC4417851EE");
}

TEST(EmitterAVX, RIP) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::loadvf_rip_plus_s32(XMM0 + 3, -123));
  tester.emit(IGen::loadvf_rip_plus_s32(XMM0 + 13, -123));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5F8281D85FFFFFFC578282D85FFFFFF");
}

TEST(EmitterAVX, ITOF) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::itof_vf(XMM0 + 3, XMM0 + 4));
  tester.emit(IGen::itof_vf(XMM0 + 3, XMM0 + 14));
  tester.emit(IGen::itof_vf(XMM0 + 13, XMM0 + 4));
  tester.emit(IGen::itof_vf(XMM0 + 13, XMM0 + 14));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5F85BDCC4C1785BDEC5785BECC441785BEE");
}

TEST(EmitterAVX, FTOI) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::ftoi_vf(XMM0 + 3, XMM0 + 4));
  tester.emit(IGen::ftoi_vf(XMM0 + 3, XMM0 + 14));
  tester.emit(IGen::ftoi_vf(XMM0 + 13, XMM0 + 4));
  tester.emit(IGen::ftoi_vf(XMM0 + 13, XMM0 + 14));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5FA5BDCC4C17A5BDEC57A5BECC4417A5BEE");
}

TEST(EmitterAVX, VPSRAD) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pw_sra(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::pw_sra(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::pw_sra(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::pw_sra(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5E172E403C4C16172E604C59172E405C4C11172E606");
}

TEST(EmitterAVX, VPSRLD) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pw_srl(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::pw_srl(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::pw_srl(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::pw_srl(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5E172D403C4C16172D604C59172D405C4C11172D606");
}

TEST(EmitterAVX, VPSLLD) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pw_sll(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::pw_sll(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::pw_sll(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::pw_sll(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5E172F403C4C16172F604C59172F405C4C11172F606");
}

// TODO - add tests

TEST(EmitterAVX, VPUNPCKLDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pextlw_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pextlw_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pextlw_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pextlw_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::pextlw_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pextlw_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pextlw_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pextlw_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E162DBC4C16162DDC59162DBC4C11162DDC56162EBC4416162EDC51162EBC4411162ED");
}

TEST(EmitterAVX, VPUNPCKHDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pextuw_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pextuw_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pextuw_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pextuw_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::pextuw_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pextuw_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pextuw_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pextuw_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E16ADBC4C1616ADDC5916ADBC4C1116ADDC5616AEBC441616AEDC5116AEBC441116AED");
}

TEST(EmitterAVX, VPUNPCKLQDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pcpyld_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pcpyld_swapped(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E16CDBC4C1616CDDC5916CDBC4C1116CDDC5616CEBC441616CEDC5116CEBC441116CED");
}

TEST(EmitterAVX, VPUNPCKHQDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pcpyud(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pcpyud(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pcpyud(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pcpyud(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::pcpyud(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pcpyud(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pcpyud(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pcpyud(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E16DDBC4C1616DDDC5916DDBC4C1116DDDC5616DEBC441616DEDC5116DEBC441116DED");
}

TEST(EmitterAVX, VPCMPEQD) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::pceqw(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pceqw(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pceqw(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pceqw(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::pceqw(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::pceqw(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::pceqw(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::pceqw(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E176DBC4C16176DDC59176DBC4C11176DDC56176EBC4416176EDC51176EBC4411176ED");
}

TEST(EmitterAVX, VPSRLDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::vpsrldq(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::vpsrldq(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::vpsrldq(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::vpsrldq(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5E173DC03C4C16173DE04C59173DC05C4C11173DE06");
}

TEST(EmitterAVX, VPSLLDQ) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::vpslldq(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::vpslldq(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::vpslldq(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::vpslldq(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5E173FC03C4C16173FE04C59173FC05C4C11173FE06");
}

TEST(EmitterAVX, VPSHUFLW) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::vpshuflw(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::vpshuflw(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::vpshuflw(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::vpshuflw(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5FB70DC03C4C17B70DE04C57B70EC05C4417B70EE06");
}

TEST(EmitterAVX, VPSHUFHW) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::vpshufhw(XMM0 + 3, XMM0 + 4, 3));
  tester.emit(IGen::vpshufhw(XMM0 + 3, XMM0 + 14, 4));
  tester.emit(IGen::vpshufhw(XMM0 + 13, XMM0 + 4, 5));
  tester.emit(IGen::vpshufhw(XMM0 + 13, XMM0 + 14, 6));
  EXPECT_EQ(tester.dump_to_hex_string(true), "C5FA70DC03C4C17A70DE04C57A70EC05C4417A70EE06");
}

TEST(EmitterAVX, movq_to_gpr_from_xmm) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::movq_gpr64_xmm64(RSP, XMM0 + 3));
  tester.emit(IGen::movq_gpr64_xmm64(RSP, XMM0 + 13));
  tester.emit(IGen::movq_gpr64_xmm64(R12, XMM0 + 3));
  tester.emit(IGen::movq_gpr64_xmm64(R12, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true), "66480F7EDC664C0F7EEC66490F7EDC664D0F7EEC");
}

TEST(EmitterAVX, movq_to_xmm_from_gpr) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::movq_xmm64_gpr64(XMM0 + 3, RSP));
  tester.emit(IGen::movq_xmm64_gpr64(XMM0 + 13, RSP));
  tester.emit(IGen::movq_xmm64_gpr64(XMM0 + 3, R12));
  tester.emit(IGen::movq_xmm64_gpr64(XMM0 + 13, R12));
  EXPECT_EQ(tester.dump_to_hex_string(true), "66480F6EDC664C0F6EEC66490F6EDC664D0F6EEC");
}

TEST(EmitterAVX, VPSUBD) {
  CodeTester tester;
  tester.init_code_buffer(1024);
  tester.emit(IGen::vpsubd(XMM0 + 3, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::vpsubd(XMM0 + 3, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::vpsubd(XMM0 + 3, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::vpsubd(XMM0 + 3, XMM0 + 13, XMM0 + 13));
  tester.emit(IGen::vpsubd(XMM0 + 13, XMM0 + 3, XMM0 + 3));
  tester.emit(IGen::vpsubd(XMM0 + 13, XMM0 + 3, XMM0 + 13));
  tester.emit(IGen::vpsubd(XMM0 + 13, XMM0 + 13, XMM0 + 3));
  tester.emit(IGen::vpsubd(XMM0 + 13, XMM0 + 13, XMM0 + 13));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "C5E1FADBC4C161FADDC591FADBC4C111FADDC561FAEBC44161FAEDC511FAEBC44111FAED");
}
