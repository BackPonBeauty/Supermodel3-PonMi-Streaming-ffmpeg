/*
 * Spikeout8Link.h
 *
 * Phase 1 of the Spikeout Final Edition (spikeofe) 8-cabinet link patch.
 * Applied to the fixed CROM only when Spikeout8Link=1 is set in the config.
 *
 * The game's link library already accepts node IDs/totals 1..8, but:
 *   - total_nodes is capped to 4 at 0x6D6D8 (5+ jumps to the fatal handler)
 *   - the per-node pointer table (0x140014 + 4*k), per-node packets
 *     (0x140400 + k*0x300) and per-node receive copies (0x142000 + k*0x900)
 *     are packed for 4 nodes and overlap other data when extended to 8.
 * These patches raise the cap to 8 and move all three areas up by 0x10000
 * (0x150014 / 0x150400 / 0x152000), which is unused RAM.
 *
 * Addresses are main RAM addresses of the program, which the boot code copies
 * from fixed CROM 0xFFE00000 (crom offset 0x600000 + address).
 * See Docs/spikeofe_8link_feasibility.md.
 */

#ifndef INCLUDED_SPIKEOUT8LINK_H
#define INCLUDED_SPIKEOUT8LINK_H

#include <cstdint>

struct Spikeout8LinkPatch
{
  uint32_t addr;
  uint32_t oldValue;
  uint32_t newValue;
};

static const Spikeout8LinkPatch s_spikeout8LinkPatches[] =
{
  { 0x6D6D8, 0x2C130004, 0x2C130008 },  // total_nodes cap 4->8
  { 0x23508, 0x3E600014, 0x3E600015 },  // node packet base 0x140400->0x150400
  { 0x5B544, 0x3E000014, 0x3E000015 },  // node packet base 0x140400->0x150400
  { 0x6D408, 0x3D200014, 0x3D200015 },  // node packet base 0x140400->0x150400
  { 0x6D6AC, 0x3C800014, 0x3C800015 },  // node packet base 0x140400->0x150400
  { 0x6D6EC, 0x3E200014, 0x3E200015 },  // node packet base 0x140400->0x150400
  { 0x6EF60, 0x3C600014, 0x3C600015 },  // node packet base 0x140400->0x150400
  { 0x6F020, 0x3C600014, 0x3C600015 },  // node packet base 0x140400->0x150400
  { 0x6F050, 0x3C600014, 0x3C600015 },  // node packet base 0x140400->0x150400
  { 0x6F0E8, 0x3C600014, 0x3C600015 },  // node packet base 0x140400->0x150400
  { 0x6F12C, 0x3C600014, 0x3C600015 },  // node packet base 0x140400->0x150400
  { 0x6F2F0, 0x3E000014, 0x3E000015 },  // node packet base 0x140400->0x150400
  { 0x6D434, 0x3C630014, 0x3C630015 },  // node packet base (addis) 0x140400->0x150400
  { 0x6EF3C, 0x3C600014, 0x3C600015 },  // rx copy base 0x142000->0x152000
  { 0x6F010, 0x3C600014, 0x3C600015 },  // rx copy base 0x142000->0x152000
  { 0x6F0D8, 0x3C600014, 0x3C600015 },  // rx copy base 0x142000->0x152000
  { 0x6D6E4, 0x3E400014, 0x3E400015 },  // node ptr table writer 0x140014->0x150014
  { 0x12160, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x12960, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x15804, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x15DDC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x15E54, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x16130, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x1622C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x1A3C8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x2351C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x496E0, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4975C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x499D8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4A82C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4A86C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4A99C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AA30, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AA94, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AD48, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AED8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AF78, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4AFFC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4B080, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4B118, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x4CB94, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x550E8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5B188, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5B274, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5B75C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5B8FC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5C3C8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x5C5F0, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x609BC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6DC54, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6DDF4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6DE30, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6DEB0, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E164, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E1D8, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E220, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E304, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E41C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E48C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E594, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E63C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E738, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6E8D4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6EA04, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6EA50, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6EB88, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6ED9C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F47C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F4B4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F544, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F5E4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F704, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F750, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6F914, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6FA14, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6FB24, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6FB54, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x6FE34, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x73364, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x73418, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x734DC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x73518, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x735B4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x73708, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x7421C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x743C4, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x7496C, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x74D10, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x750BC, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x75254, 0x3C420014, 0x3C420015 },  // node ptr table read
  { 0x755E0, 0x3C420014, 0x3C420015 },  // node ptr table read
  // 11/17 (owner): the WARNING screen at power-on stays, but 1 second instead of 10. The game mode byte 0x104008
  // (0 WARNING_INT, 1 WARNING_DSP, 2 ADV_INT ...) moves on when the countdown 0x10400C (SDA -0x3FF4, -1 a frame at
  // 0x49350) runs out. WARNING_INT (0x49394) sets it to 0x23F = 575 frames when the "SEGA" mark at 0x1006A0 is not
  // there yet and 0x100198 == 1, else 3 (WatchWrite 104008: WARNING_DSP frames 260-834). 575 -> 57 (1 s at 57.5 Hz).
  { 0x493CC, 0x3800023F, 0x38000039 },  // WARNING screen 575 -> 57 frames
};

/*
 * Phase 2 (work in progress): players 5-8 use character IDs 4-7, which are
 * enemy indices 0-3. Applied on top of Phase 1 when Spikeout8Players=1.
 * Code caves live in the unused 0x3070-0x3FFF gap of the program image.
 */
static const Spikeout8LinkPatch s_spikeout8PlayersPatches[] =
{
  { 0x03080, 0x00000000, 0x7E86A0F8 },  // cave: free-slot search value without enemy 0-3 (general allocator)
  { 0x03084, 0x00000000, 0x54C6013E },  // cave: free-slot search value without enemy 0-3 (general allocator)
  { 0x03088, 0x00000000, 0x48057B60 },  // cave: free-slot search value without enemy 0-3 (general allocator)
  { 0x5ABE4, 0x7E86A0F8, 0x4BFA849C },  // general enemy allocator: skip enemy index 0-3
  { 0x03090, 0x00000000, 0x7E7398F8 },  // cave: free-slot search value without enemy 0-3 (list spawn)
  { 0x03094, 0x00000000, 0x5673013E },  // cave: free-slot search value without enemy 0-3 (list spawn)
  { 0x03098, 0x00000000, 0x4805A068 },  // cave: free-slot search value without enemy 0-3 (list spawn)
  { 0x5D0FC, 0x7E7398F8, 0x4BFA5F94 },  // list enemy spawn: skip enemy index 0-3
  { 0x8CFD0, 0x00000080, 0x00000100 },  // fa table: fa_player work size 0x80 -> 0x100
  { 0x1AC04, 0x38630060, 0x386300A0 },  // fa_player +60 -> +A0
  { 0x1C550, 0x38630060, 0x386300A0 },  // fa_player +60 -> +A0
  { 0x1D204, 0x38630060, 0x386300A0 },  // fa_player +60 -> +A0
  { 0x1E28C, 0x38840040, 0x38840070 },  // fa_player +40 -> +70
  { 0x22C88, 0x3863002C, 0x3863004C },  // fa_player +2C -> +4C
  { 0x233C8, 0x80870050, 0x80870090 },  // fa_player +50 -> +90
  { 0x234CC, 0x80830050, 0x80830090 },  // fa_player +50 -> +90
  { 0x2A880, 0x38630060, 0x386300A0 },  // fa_player +60 -> +A0
  { 0x30454, 0x81E30040, 0x81E30070 },  // fa_player +40 -> +70
  { 0x4A5CC, 0x90830058, 0x90830098 },  // fa_player +58 -> +98
  { 0x4A804, 0x90830060, 0x908300A0 },  // fa_player +60 -> +A0
  { 0x51398, 0x38EC002C, 0x38EC004C },  // fa_player +2C -> +4C
  { 0x5139C, 0x38CC003C, 0x38CC006C },  // fa_player +3C -> +6C
  { 0x51400, 0x3A2C001C, 0x3A2C002C },  // fa_player +1C -> +2C
  { 0x51404, 0x3A4C002C, 0x3A4C004C },  // fa_player +2C -> +4C
  { 0x5140C, 0x3A8C003C, 0x3A8C006C },  // fa_player +3C -> +6C
  { 0x514B8, 0x800C0050, 0x800C0090 },  // fa_player +50 -> +90
  { 0x514BC, 0x92EC0050, 0x92EC0090 },  // fa_player +50 -> +90
  { 0x514C0, 0x900C005C, 0x900C009C },  // fa_player +5C -> +9C
  { 0x514F8, 0x38C50030, 0x38C50050 },  // fa_player +30 -> +50
  { 0x51508, 0x38C50020, 0x38C50030 },  // fa_player +20 -> +30
  { 0x515E8, 0x38C50030, 0x38C50050 },  // fa_player +30 -> +50
  { 0x516F8, 0x808C0058, 0x808C0098 },  // fa_player +58 -> +98
  { 0x517B8, 0x906C0058, 0x906C0098 },  // fa_player +58 -> +98
  { 0x517F0, 0x3A430050, 0x3A430090 },  // fa_player +50 -> +90
  { 0x53ED8, 0x81030050, 0x81030090 },  // fa_player +50 -> +90
  { 0x5507C, 0x80830050, 0x80830090 },  // fa_player +50 -> +90
  { 0x55404, 0x80830050, 0x80830090 },  // fa_player +50 -> +90
  { 0x5A25C, 0x90830060, 0x908300A0 },  // fa_player +60 -> +A0
  { 0x5B3CC, 0x38630060, 0x386300A0 },  // fa_player +60 -> +A0
  { 0x5BD40, 0x82030050, 0x82030090 },  // fa_player +50 -> +90
  { 0x5BD5C, 0x2C030004, 0x2C030008 },  // area event script pause: check entering players 0-7 (node 5-8 host stalled area 95 intro forever)
  { 0x5BF38, 0x80830050, 0x80830090 },  // fa_player +50 -> +90
  { 0x5BF3C, 0x80630054, 0x80630094 },  // fa_player +54 -> +94
  { 0x5D4A0, 0x82740050, 0x82740090 },  // fa_player +50 -> +90
  { 0x5D4AC, 0x80740054, 0x80740094 },  // fa_player +54 -> +94
  { 0x5D4B4, 0x90740054, 0x90740094 },  // fa_player +54 -> +94
  { 0x5D4E4, 0x82740054, 0x82740094 },  // fa_player +54 -> +94
  { 0x5D5F4, 0x3A63002C, 0x3A63004C },  // fa_player +2C -> +4C
  { 0x5D62C, 0x92830054, 0x92830094 },  // fa_player +54 -> +94
  { 0x5DC40, 0x82620050, 0x82620090 },  // fa_player +50 -> +90
  { 0x5E7C8, 0x82740050, 0x82740090 },  // fa_player +50 -> +90
  { 0x5E7D4, 0x80740054, 0x80740094 },  // fa_player +54 -> +94
  { 0x5E7DC, 0x90740054, 0x90740094 },  // fa_player +54 -> +94
  { 0x64BC8, 0x82110050, 0x82110090 },  // fa_player +50 -> +90
  { 0x64BCC, 0x8071005C, 0x8071009C },  // fa_player +5C -> +9C
  { 0x67E0C, 0x90A60058, 0x90A60098 },  // fa_player +58 -> +98
  { 0x67FB4, 0x90E50058, 0x90E50098 },  // fa_player +58 -> +98
  { 0x68134, 0x38C60030, 0x38C60050 },  // fa_player +30 -> +50
  { 0x68224, 0x80C50058, 0x80C50098 },  // fa_player +58 -> +98
  { 0x6822C, 0x90C50058, 0x90C50098 },  // fa_player +58 -> +98
  { 0x68CC0, 0x80C50058, 0x80C50098 },  // fa_player +58 -> +98
  { 0x68CD4, 0x90C50058, 0x90C50098 },  // fa_player +58 -> +98
  { 0x68F30, 0x90C50058, 0x90C50098 },  // fa_player +58 -> +98
  { 0x693C0, 0x80640058, 0x80640098 },  // fa_player +58 -> +98
  { 0x693C8, 0x90640058, 0x90640098 },  // fa_player +58 -> +98
  { 0x69CDC, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x69F90, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x6A298, 0x3A440040, 0x3A440070 },  // fa_player +40 -> +70
  { 0x6A7A0, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x6AC94, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x6AEF8, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x6B22C, 0x90030058, 0x90030098 },  // fa_player +58 -> +98
  { 0x72B98, 0x3A94002C, 0x3A94004C },  // fa_player +2C -> +4C
  { 0x72BF4, 0x38840060, 0x388400A0 },  // fa_player +60 -> +A0
  { 0x72C2C, 0x3A94002C, 0x3A94004C },  // fa_player +2C -> +4C
  { 0x732E4, 0x90830050, 0x90830090 },  // fa_player +50 -> +90
  { 0x732E8, 0x90830054, 0x90830094 },  // fa_player +54 -> +94
  { 0x734C0, 0x3AF7005F, 0x3AF7009F },  // fa_player +5F -> +9F
  { 0x74F14, 0x80840050, 0x80840090 },  // fa_player +50 -> +90
  { 0x75E50, 0x80C50058, 0x80C50098 },  // fa_player +58 -> +98
  { 0x75E60, 0x90C50058, 0x90C50098 },  // fa_player +58 -> +98
  { 0x75F9C, 0x80C50058, 0x80C50098 },  // fa_player +58 -> +98
  { 0x75FAC, 0x90C50058, 0x90C50098 },  // fa_player +58 -> +98
  { 0x22C8C, 0x38800004, 0x38800008 },  // pending player request check: 8 players
  { 0x513A0, 0x38600004, 0x38600008 },  // fa_player init: clear 8 entries
  { 0x51410, 0x3AA00004, 0x3AA00008 },  // fa_player task: 8 players
  { 0x51420, 0x22D50004, 0x22D50008 },  // fa_player task: player index = 8 - counter
  { 0x5165C, 0x38800004, 0x38800008 },  // player flag sync loop: 8 players
  { 0x517AC, 0x2C030004, 0x2C030008 },  // player flag sync (masked) loop: 8 players
  { 0x517E4, 0x3DC0001B, 0x3DC0001C },  // reverse player loop: start after player 8 (0x1C0000)
  { 0x517E8, 0x3BE00003, 0x3BE00007 },  // reverse player loop: 8 players
  { 0x517EC, 0x3A030020, 0x3A030030 },  // reverse player loop: end of 8-entry state array
  { 0x5186C, 0x3DE0001B, 0x3DE0001C },  // reverse player loop 2: start after player 8 (0x1C0000)
  { 0x51870, 0x3BE00003, 0x3BE00007 },  // reverse player loop 2: 8 players
  { 0x51874, 0x3A030020, 0x3A030030 },  // reverse player loop 2: end of 8-entry state array
  { 0x51934, 0x2C130004, 0x2C130008 },  // player delete-request loop: 8 players
  { 0x030A0, 0x00000000, 0x38800004 },  // cave: Rob+0x3F00 slot numbers for players 5-8 (slot 4)
  { 0x030A4, 0x00000000, 0x9C834000 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x030A8, 0x00000000, 0x38800005 },  // cave: Rob+0x3F00 slot numbers for players 5-8 (slot 5)
  { 0x030AC, 0x00000000, 0x9C834000 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x030B0, 0x00000000, 0x38800006 },  // cave: Rob+0x3F00 slot numbers for players 5-8 (slot 6)
  { 0x030B4, 0x00000000, 0x9C834000 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x030B8, 0x00000000, 0x38800007 },  // cave: Rob+0x3F00 slot numbers for players 5-8 (slot 7)
  { 0x030BC, 0x00000000, 0x9C834000 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x030C0, 0x00000000, 0x80010000 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x030C4, 0x00000000, 0x48072C98 },  // cave: Rob+0x3F00 slot numbers for players 5-8
  { 0x75D58, 0x80010000, 0x4BF8D348 },  // character select init: also set players 5-8
  { 0x49EE8, 0x38800000, 0x38800004 },  // attract demo enemy: fixed index 0 -> 4
  { 0x49EFC, 0x38800001, 0x38800005 },  // attract demo enemy: fixed index 1 -> 5
  { 0x49F10, 0x38800002, 0x38800006 },  // attract demo enemy: fixed index 2 -> 6
  { 0x49F24, 0x38800003, 0x38800007 },  // attract demo enemy: fixed index 3 -> 7
  { 0x49F38, 0x38800004, 0x38800008 },  // attract demo enemy: fixed index 4 -> 8
  { 0x49F4C, 0x38800005, 0x38800009 },  // attract demo enemy: fixed index 5 -> 9
  { 0x49F60, 0x38800006, 0x3880000A },  // attract demo enemy: fixed index 6 -> 10
  { 0x49F74, 0x38800007, 0x3880000B },  // attract demo enemy: fixed index 7 -> 11
  { 0x49FCC, 0x38800000, 0x38800004 },  // attract demo enemy: fixed index 0 -> 4
  { 0x49FE0, 0x38800001, 0x38800005 },  // attract demo enemy: fixed index 1 -> 5
  { 0x49FF4, 0x38800002, 0x38800006 },  // attract demo enemy: fixed index 2 -> 6
  { 0x4A008, 0x38800003, 0x38800007 },  // attract demo enemy: fixed index 3 -> 7
  { 0x4A01C, 0x38800004, 0x38800008 },  // attract demo enemy: fixed index 4 -> 8
  { 0x4A030, 0x38800005, 0x38800009 },  // attract demo enemy: fixed index 5 -> 9
  { 0x4A044, 0x38800006, 0x3880000A },  // attract demo enemy: fixed index 6 -> 10
  { 0x4A058, 0x38800007, 0x3880000B },  // attract demo enemy: fixed index 7 -> 11
  { 0x03128, 0x00000000, 0x40820008 },  // cave: skip player whose state is clear or whose handler is 0
  { 0x0312C, 0x00000000, 0x4804E36C },  // cave: skip player whose state is clear or whose handler is 0
  { 0x03130, 0x00000000, 0x2C040000 },  // cave: skip player whose state is clear or whose handler is 0
  { 0x03134, 0x00000000, 0x40820008 },  // cave: skip player whose state is clear or whose handler is 0
  { 0x03138, 0x00000000, 0x4804E360 },  // cave: skip player whose state is clear or whose handler is 0
  { 0x0313C, 0x00000000, 0x4804E348 },  // cave: skip player whose state is clear or whose handler is 0
  { 0x51480, 0x41820018, 0x4BFB1CA8 },  // fa_player task: skip players without a handler
  { 0x6DDA4, 0x54A50006, 0x54A5000E },  // link in-game node mask: 0xF0000000 -> 0xFF000000
  { 0x6DDD0, 0x564A0006, 0x564A000E },  // link in-game node mask: 0xF0000000 -> 0xFF000000
  { 0x6DE0C, 0x54840006, 0x5484000E },  // link in-game node mask: 0xF0000000 -> 0xFF000000
  { 0x6F634, 0x53C30006, 0x53C3000E },  // link in-game node mask: 0xF0000000 -> 0xFF000000
  { 0x6F794, 0x52630006, 0x5263000E },  // link in-game node mask: 0xF0000000 -> 0xFF000000
  { 0x6E3F4, 0x76B5F000, 0x76B5FF00 },  // link in-game node mask: andis 0xF000 -> 0xFF00
  { 0x732DC, 0x7631F000, 0x7631FF00 },  // link in-game node mask: andis 0xF000 -> 0xFF00
  { 0x72BD4, 0x2C120004, 0x2C120008 },  // save: in-game player mask over 8 players
  { 0x03150, 0x00000000, 0x3880FFFF },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x03154, 0x00000000, 0x3CA00016 },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x03158, 0x00000000, 0x60A50C00 },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x0315C, 0x00000000, 0x90850000 },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x03160, 0x00000000, 0x90850004 },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x03164, 0x00000000, 0x4806FA8C },  // cave: save: 8-byte fa_player+0xA0 copy area
  { 0x72BE4, 0x3880FFFF, 0x4BF9056C },  // save: 8-byte fa_player+0xA0 copy area
  { 0x72C08, 0x2C060004, 0x2C060008 },  // save: copy fa_player+0xA0 for 8 players
  { 0x72C20, 0x3A400004, 0x3A400008 },  // save: 8 player records
  { 0x03168, 0x00000000, 0x3E600016 },  // cave: save: player records base -> 0x160008
  { 0x0316C, 0x00000000, 0x62730008 },  // cave: save: player records base -> 0x160008
  { 0x03170, 0x00000000, 0x4806FAB8 },  // cave: save: player records base -> 0x160008
  { 0x72C24, 0x3A700008, 0x4BF90544 },  // save: player records base -> 0x160008
  { 0x03174, 0x00000000, 0x3E600016 },  // cave: save: player blocks base -> 0x160200
  { 0x03178, 0x00000000, 0x62730100 },  // cave: save: player blocks base -> 0x160200
  { 0x0317C, 0x00000000, 0x4806FB6C },  // cave: save: player blocks base -> 0x160200
  { 0x72CE4, 0x3A700228, 0x4BF90490 },  // save: player blocks base -> 0x160200
  { 0x72D08, 0x2C120004, 0x2C120008 },  // save: 8 player blocks
  { 0x03180, 0x00000000, 0x3C800016 },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x03184, 0x00000000, 0x80A40C00 },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x03188, 0x00000000, 0x90A300A0 },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x0318C, 0x00000000, 0x80A40C04 },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x03190, 0x00000000, 0x90A300A4 },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x03194, 0x00000000, 0x480570CC },  // cave: restore: 8-byte fa_player+0xA0 copy
  { 0x5A258, 0x809F1728, 0x4BFA8F28 },  // restore: 8-byte fa_player+0xA0 copy
  { 0x03198, 0x00000000, 0x3F400016 },  // cave: restore: player blocks base -> 0x160200
  { 0x0319C, 0x00000000, 0x635A0100 },  // cave: restore: player blocks base -> 0x160200
  { 0x031A0, 0x00000000, 0x480570C4 },  // cave: restore: player blocks base -> 0x160200
  { 0x5A260, 0x3B5F0228, 0x4BFA8F38 },  // restore: player blocks base -> 0x160200
  { 0x5A29C, 0x2C1B0004, 0x2C1B0008 },  // restore: 8 player blocks
  { 0x031A4, 0x00000000, 0x3FA00016 },  // cave: restore: player records base -> 0x160008
  { 0x031A8, 0x00000000, 0x63BD0008 },  // cave: restore: player records base -> 0x160008
  { 0x031AC, 0x00000000, 0x48057100 },  // cave: restore: player records base -> 0x160008
  { 0x5A2A8, 0x3BBF0008, 0x4BFA8EFC },  // restore: player records base -> 0x160008
  { 0x5A2C0, 0x2C040004, 0x2C040008 },  // restore: create up to 8 players
  { 0x031B0, 0x00000000, 0x3CC00016 },  // cave: newcomer: player records base -> 0x160008
  { 0x031B4, 0x00000000, 0x60C60008 },  // cave: newcomer: player records base -> 0x160008
  { 0x031B8, 0x00000000, 0x480704E8 },  // cave: newcomer: player records base -> 0x160008
  { 0x7369C, 0x38D00008, 0x4BF8FB14 },  // newcomer: player records base -> 0x160008
  { 0x031BC, 0x00000000, 0x388DEE20 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031C0, 0x00000000, 0x7C6438AE },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031C4, 0x00000000, 0x60000000 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031C8, 0x00000000, 0x2C110004 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031CC, 0x00000000, 0x41800010 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031D0, 0x00000000, 0x3C800016 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031D4, 0x00000000, 0x60840C3C },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031D8, 0x00000000, 0x4800000C },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031DC, 0x00000000, 0x3C800036 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031E0, 0x00000000, 0x608453E0 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031E4, 0x00000000, 0x7C6489AE },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031E8, 0x00000000, 0x480704D4 },  // cave: newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x736A8, 0x388DC07C, 0x4BF8FB14 },  // newcomer: slot number via node&3, slot 4-7 to spill area
  { 0x031EC, 0x00000000, 0x3C600016 },  // cave: newcomer: player blocks base -> 0x160200
  { 0x031F0, 0x00000000, 0x60630200 },  // cave: newcomer: player blocks base -> 0x160200
  { 0x031F4, 0x00000000, 0x48070534 },  // cave: newcomer: player blocks base -> 0x160200
  { 0x73724, 0x38700328, 0x4BF8FAC8 },  // newcomer: player blocks base -> 0x160200
  { 0x031F8, 0x00000000, 0x2C110004 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x031FC, 0x00000000, 0x41800010 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x03200, 0x00000000, 0x3C800016 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x03204, 0x00000000, 0x60840C18 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x03208, 0x00000000, 0x48070558 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x0320C, 0x00000000, 0x38900398 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x03210, 0x00000000, 0x48070550 },  // cave: newcomer: 0x365398 slot 4-7 to spill area
  { 0x7375C, 0x38900398, 0x4BF8FA9C },  // newcomer: 0x365398 slot 4-7 to spill area
  { 0x03214, 0x00000000, 0x2C110004 },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x03218, 0x00000000, 0x41800010 },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x0321C, 0x00000000, 0x3C800016 },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x03220, 0x00000000, 0x60840C2C },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x03224, 0x00000000, 0x4807055C },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x03228, 0x00000000, 0x389003A0 },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x0322C, 0x00000000, 0x48070554 },  // cave: newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x7377C, 0x389003A0, 0x4BF8FA98 },  // newcomer: 0x3653A0 slot 4-7 to spill area
  { 0x03230, 0x00000000, 0x3863FFFF },  // cave: per-node table (0x28): index (node-1)&3
  { 0x03234, 0x00000000, 0x546307BE },  // cave: per-node table (0x28): index (node-1)&3
  { 0x03238, 0x00000000, 0x4802CD28 },  // cave: per-node table (0x28): index (node-1)&3
  { 0x2FF5C, 0x3863FFFF, 0x4BFD32D4 },  // per-node table (0x28): index (node-1)&3
  { 0x5D4BC, 0x2C030004, 0x2C030008 },  // area entry (0x5D49C): delete up to 8 players
  { 0x5D4F0, 0x2C040004, 0x2C040008 },  // area entry (0x5D49C): create up to 8 players
  { 0x03250, 0x00000000, 0x548307BE },  // cave: area entry (0x5D49C): start position of player idx&3
  { 0x03254, 0x00000000, 0x1C630028 },  // cave: area entry (0x5D49C): start position of player idx&3
  { 0x03258, 0x00000000, 0x4805A2B0 },  // cave: area entry (0x5D49C): start position of player idx&3
  { 0x5D504, 0x1C640028, 0x4BFA5D4C },  // area entry (0x5D49C): start position of player idx&3
  { 0x5D5E8, 0x2C150004, 0x2C150008 },  // player cleanup (0x5D594): 8 players
  { 0x5D620, 0x2C150004, 0x2C150008 },  // player cleanup (0x5D594): rebuild mask for 8 players
  { 0x5BF24, 0x3A40000C, 0x3E600016 },  // area health recovery: lis r19, 0x16
  { 0x5BF2C, 0x3A6DC0D8, 0x62730170 },  // area health recovery: ori r19, r19, 0x170 (r19 = 0x160170)
  { 0x5BF54, 0x2C140004, 0x2C140008 },  // player re-create after area change: 8 players
  { 0x5BF90, 0x386DC0A8, 0x38730010 },  // area health recovery: addi r3, r19, 0x10 (r3 = 0x160180)
  { 0x5E7E4, 0x2C030004, 0x2C030008 },  // area entry (0x5E7C4): delete up to 8 players
  { 0x5E81C, 0x2C040004, 0x2C040008 },  // area entry (0x5E7C4): create up to 8 players
  { 0x0325C, 0x00000000, 0x548307BE },  // cave: area entry (0x5E7C4): start position of player idx&3
  { 0x03260, 0x00000000, 0x1C630028 },  // cave: area entry (0x5E7C4): start position of player idx&3
  { 0x03264, 0x00000000, 0x4805B5D0 },  // cave: area entry (0x5E7C4): start position of player idx&3
  { 0x5E830, 0x1C640028, 0x4BFA4A2C },  // area entry (0x5E7C4): start position of player idx&3
  { 0x11C5C, 0x388DE2AE, 0x388DEDFE },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x2A200, 0x386DE2B0, 0x386DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5B5D8, 0x386DE2AE, 0x386DEDFE },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5B624, 0x3B2DE2B8, 0x3B2DEE10 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5B7A0, 0x386DE2B0, 0x386DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5B984, 0x3AADE2B0, 0x3AADEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5BAAC, 0x3A8DE2B0, 0x3A8DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5CC14, 0x3BEDE2B8, 0x3BEDEE10 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x6FDE0, 0xA08DE2B0, 0xA08DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x6FDE4, 0xA0ADE2B2, 0xA0ADEE02 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x6FDE8, 0xA0CDE2B4, 0xA0CDEE04 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x6FDEC, 0xA0EDE2B6, 0xA0EDEE06 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x6FE10, 0x3A8DE2B0, 0x3A8DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x728B8, 0xA06DE2B0, 0xA06DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x728BC, 0xA08DE2B2, 0xA08DEE02 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x728C0, 0xA0ADE2B4, 0xA0ADEE04 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x728C4, 0xA0CDE2B6, 0xA0CDEE06 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x74EE0, 0x388DE2B0, 0x388DEE00 },  // player time arrays -> 8 entries (0x106E00/0x106E10)
  { 0x5B5F0, 0x2C850004, 0x2C850008 },  // player time countdown: 8 players
  { 0x28BAC, 0x2C150004, 0x2C150008 },  // aim/distance table (0x28B54): compute rows for 8 player subjects
  { 0x28B2C, 0x3A600003, 0x3A600007 },  // aim/distance table (0x28A9C player-player): 8 players (triangular)
  // Aim/distance table (0x28B54): loop over enemies 4-15 only (skip 0-3 which are players 5-8)
  { 0x28B68, 0x3A23000C, 0x3A23001C },  // player-enemy distance loop: fa_enemy offset 0xC -> 0x1C (Enemy 4)
  { 0x28B6C, 0x3DE0001B, 0x3DE0001C },  // player-enemy distance loop: enemy Rob base 0x1B0000 -> 0x1C0000 (Enemy 4)
  { 0x28B80, 0x3AD40004, 0x3AD40008 },  // player-enemy distance loop: slot index r20 + 8 (Enemies 4-15 = slots 8-19)
  { 0x28BA0, 0x2C140010, 0x2C14000C },  // player-enemy distance loop: count 16 -> 12 (Enemies 4-15)
  { 0x27274, 0x39000004, 0x39000008 },  // collision detection (0x2725C): 8 players in the player loop
  // Collision detection (0x2725C): loop over enemies 4-15 only (skip 0-3 which are players 5-8)
  { 0x272C4, 0x3BC3000C, 0x3BC3001C },  // player-enemy loop: fa_enemy offset 0xC -> 0x1C (Enemy 4)
  { 0x272C8, 0x3DE0001B, 0x3DE0001C },  // player-enemy loop: enemy Rob base 0x1B0000 -> 0x1C0000 (Enemy 4)
  { 0x272D0, 0x38000010, 0x3800000C },  // player-enemy loop: count 16 -> 12 (Enemies 4-15)
  { 0x27308, 0x3923000C, 0x3923001C },  // enemy-enemy loop: fa_enemy offset 0xC -> 0x1C (Enemy 4)
  { 0x2730C, 0x3DC0001B, 0x3DC0001C },  // enemy-enemy loop: enemy Rob base 0x1B0000 -> 0x1C0000 (Enemy 4)
  { 0x27314, 0x39000010, 0x3900000C },  // enemy-enemy loop: count 16 -> 12 (Enemies 4-15)
  { 0x26EFC, 0x3CE0F000, 0x3CE0FF00 },  // attack target mask (enemy): slots 0-7 players + objects (0xFF000FFF)
  { 0x26EF0, 0x3CE01000, 0x3CE00100 },  // attack target mask (player): slots 8-31 enemies + objects (0x00FFFFFF)
  { 0x26F28, 0x3CE01000, 0x3CE00100 },  // attack target mask (player special): slots 8-31 (0x00FFFFFF)
  { 0x26F40, 0x2C830004, 0x2C830008 },  // attack target friendly-check (0x26F40): 8 players
  { 0x53DA0, 0x2C030003, 0x2C030007 },  // enemy target keep (0x53DA0): consider 8 players (slot <= 7)
  { 0x53F1C, 0x2C030004, 0x2C030008 },  // enemy target pick (0x53F18): consider 8 players
  { 0x54018, 0x2C140004, 0x2C140008 },  // enemy target pick alt (0x54010): consider 8 players
  { 0x5404C, 0x3BA00004, 0x3BA00008 },  // enemy target pick fallback (0x5404C): consider 8 players
  { 0x551A4, 0x3821FFF0, 0x9421FFB0 },  // enemy target aggregation 8-player: insn 0
  { 0x551A8, 0x92810000, 0x7C0802A6 },  // enemy target aggregation 8-player: insn 1
  { 0x551AC, 0x92A10004, 0x90010048 },  // enemy target aggregation 8-player: insn 2
  { 0x551B0, 0x7C0802A6, 0x92810038 },  // enemy target aggregation 8-player: insn 3
  { 0x551B4, 0x90010008, 0x92A1003C },  // enemy target aggregation 8-player: insn 4
  { 0x551B8, 0x38600000, 0x92C10040 },  // enemy target aggregation 8-player: insn 5
  { 0x551BC, 0x906C0010, 0x92E10044 },  // enemy target aggregation 8-player: insn 6
  { 0x551C0, 0x906C0018, 0x38600000 },  // enemy target aggregation 8-player: insn 7
  { 0x551C4, 0x906C001C, 0x90610010 },  // enemy target aggregation 8-player: insn 8
  { 0x551C8, 0x906C0020, 0x90610014 },  // enemy target aggregation 8-player: insn 9
  { 0x551CC, 0x906C0024, 0x90610018 },  // enemy target aggregation 8-player: insn 10
  { 0x551D0, 0x3A8C0010, 0x9061001C },  // enemy target aggregation 8-player: insn 11
  { 0x551D4, 0x3AAC0018, 0x90610020 },  // enemy target aggregation 8-player: insn 12
  { 0x551D8, 0x3C600005, 0x90610024 },  // enemy target aggregation 8-player: insn 13
  { 0x551DC, 0x38635264, 0x90610028 },  // enemy target aggregation 8-player: insn 14
  { 0x551E0, 0x4BFFD081, 0x9061002C },  // enemy target aggregation 8-player: insn 15
  { 0x551E4, 0x3CE0001B, 0x90610030 },  // enemy target aggregation 8-player: insn 16
  { 0x551E8, 0x38E7B4C5, 0x90610034 },  // enemy target aggregation 8-player: insn 17
  { 0x551EC, 0x886C0010, 0x3A810010 },  // enemy target aggregation 8-player: insn 18
  { 0x551F0, 0x888C0011, 0x3AA10018 },  // enemy target aggregation 8-player: insn 19
  { 0x551F4, 0x88AC0012, 0x3C600005 },  // enemy target aggregation 8-player: insn 20
  { 0x551F8, 0x88CC0013, 0x38635264 },  // enemy target aggregation 8-player: insn 21
  { 0x551FC, 0x98678000, 0x4BFFD065 },  // enemy target aggregation 8-player: insn 22
  { 0x55200, 0x9887C000, 0x3EC0001A },  // enemy target aggregation 8-player: insn 23
  { 0x55204, 0x98A70000, 0x3AE00000 },  // enemy target aggregation 8-player: insn 24
  { 0x55208, 0x98C74000, 0x7C74B8AE },  // enemy target aggregation 8-player: insn 25
  { 0x5520C, 0x3CE0001B, 0x987634C5 },  // enemy target aggregation 8-player: insn 26
  { 0x55210, 0x38E7B008, 0x56E4103A },  // enemy target aggregation 8-player: insn 27
  { 0x55214, 0x806C0018, 0x7CB5202E },  // enemy target aggregation 8-player: insn 28
  { 0x55218, 0x808C001C, 0x90B63008 },  // enemy target aggregation 8-player: insn 29
  { 0x5521C, 0x80AC0020, 0x3AD64000 },  // enemy target aggregation 8-player: insn 30
  { 0x55220, 0x80CC0024, 0x3AF70001 },  // enemy target aggregation 8-player: insn 31
  { 0x55224, 0x90678000, 0x2C170008 },  // enemy target aggregation 8-player: insn 32
  { 0x55228, 0x9087C000, 0x4180FFE0 },  // enemy target aggregation 8-player: insn 33
  { 0x5522C, 0x90A70000, 0x3C600005 },  // enemy target aggregation 8-player: insn 34
  { 0x55230, 0x90C74000, 0x38635298 },  // enemy target aggregation 8-player: insn 35
  { 0x55234, 0x3C600005, 0x4BFFC599 },  // enemy target aggregation 8-player: insn 36
  { 0x55238, 0x38635298, 0x3C600005 },  // enemy target aggregation 8-player: insn 37
  { 0x5523C, 0x4BFFC591, 0x386352FC },  // enemy target aggregation 8-player: insn 38
  { 0x55240, 0x3C600005, 0x4BFFC58D },  // enemy target aggregation 8-player: insn 39
  { 0x55244, 0x386352FC, 0x80010048 },  // enemy target aggregation 8-player: insn 40
  { 0x55248, 0x4BFFC585, 0x7C0803A6 },  // enemy target aggregation 8-player: insn 41
  { 0x5524C, 0x80010008, 0x82810038 },  // enemy target aggregation 8-player: insn 42
  { 0x55250, 0x7C0803A6, 0x82A1003C },  // enemy target aggregation 8-player: insn 43
  { 0x55254, 0x82A10004, 0x82C10040 },  // enemy target aggregation 8-player: insn 44
  { 0x55258, 0x82810000, 0x82E10044 },  // enemy target aggregation 8-player: insn 45
  { 0x5525C, 0x38210010, 0x38210050 },  // enemy target aggregation 8-player: insn 46
  { 0x55260, 0x4E800020, 0x4E800020 },  // enemy target aggregation 8-player: insn 47
  { 0x03268, 0x00000000, 0x388DEE0E },  // cave: time bonus +0x500 for 8 players
  { 0x0326C, 0x00000000, 0x38A00008 },  // cave: time bonus +0x500 for 8 players
  { 0x03270, 0x00000000, 0xA4640002 },  // cave: time bonus +0x500 for 8 players
  { 0x03274, 0x00000000, 0x38630500 },  // cave: time bonus +0x500 for 8 players
  { 0x03278, 0x00000000, 0xB0640000 },  // cave: time bonus +0x500 for 8 players
  { 0x0327C, 0x00000000, 0x34A5FFFF },  // cave: time bonus +0x500 for 8 players
  { 0x03280, 0x00000000, 0x4082FFF0 },  // cave: time bonus +0x500 for 8 players
  { 0x03284, 0x00000000, 0x480511C0 },  // cave: time bonus +0x500 for 8 players
  { 0x54414, 0xA06DE2B8, 0x4BFAEE54 },  // time bonus +0x500 for 8 players
  { 0x03288, 0x00000000, 0x38ADEE00 },  // cave: clear time arrays for 8 players
  { 0x0328C, 0x00000000, 0x90850000 },  // cave: clear time arrays for 8 players
  { 0x03290, 0x00000000, 0x90850004 },  // cave: clear time arrays for 8 players
  { 0x03294, 0x00000000, 0x90850008 },  // cave: clear time arrays for 8 players
  { 0x03298, 0x00000000, 0x9085000C },  // cave: clear time arrays for 8 players
  { 0x0329C, 0x00000000, 0x90850010 },  // cave: clear time arrays for 8 players
  { 0x032A0, 0x00000000, 0x90850014 },  // cave: clear time arrays for 8 players
  { 0x032A4, 0x00000000, 0x90850018 },  // cave: clear time arrays for 8 players
  { 0x032A8, 0x00000000, 0x9085001C },  // cave: clear time arrays for 8 players
  { 0x032AC, 0x00000000, 0x48056808 },  // cave: clear time arrays for 8 players
  { 0x59A94, 0xB08DE2B0, 0x4BFA97F4 },  // clear time arrays for 8 players
  { 0x032B0, 0x00000000, 0x390DEE00 },  // cave: restore times for 8 players
  { 0x032B4, 0x00000000, 0xB0680000 },  // cave: restore times for 8 players
  { 0x032B8, 0x00000000, 0xB0880002 },  // cave: restore times for 8 players
  { 0x032BC, 0x00000000, 0xB0A80004 },  // cave: restore times for 8 players
  { 0x032C0, 0x00000000, 0xB0C80006 },  // cave: restore times for 8 players
  { 0x032C4, 0x00000000, 0x3D200016 },  // cave: restore times for 8 players
  { 0x032C8, 0x00000000, 0x61290C20 },  // cave: restore times for 8 players
  { 0x032CC, 0x00000000, 0x38E000FF },  // cave: restore times for 8 players
  { 0x032D0, 0x00000000, 0xA0690000 },  // cave: restore times for 8 players
  { 0x032D4, 0x00000000, 0x7C033800 },  // cave: restore times for 8 players
  { 0x032D8, 0x00000000, 0x40800008 },  // cave: restore times for 8 players
  { 0x032DC, 0x00000000, 0x38670000 },  // cave: restore times for 8 players
  { 0x032E0, 0x00000000, 0xB0680008 },  // cave: restore times for 8 players
  { 0x032E4, 0x00000000, 0xA0690002 },  // cave: restore times for 8 players
  { 0x032E8, 0x00000000, 0x7C033800 },  // cave: restore times for 8 players
  { 0x032EC, 0x00000000, 0x40800008 },  // cave: restore times for 8 players
  { 0x032F0, 0x00000000, 0x38670000 },  // cave: restore times for 8 players
  { 0x032F4, 0x00000000, 0xB068000A },  // cave: restore times for 8 players
  { 0x032F8, 0x00000000, 0xA0690004 },  // cave: restore times for 8 players
  { 0x032FC, 0x00000000, 0x7C033800 },  // cave: restore times for 8 players
  { 0x03300, 0x00000000, 0x40800008 },  // cave: restore times for 8 players
  { 0x03304, 0x00000000, 0x38670000 },  // cave: restore times for 8 players
  { 0x03308, 0x00000000, 0xB068000C },  // cave: restore times for 8 players
  { 0x0330C, 0x00000000, 0xA0690006 },  // cave: restore times for 8 players
  { 0x03310, 0x00000000, 0x7C033800 },  // cave: restore times for 8 players
  { 0x03314, 0x00000000, 0x40800008 },  // cave: restore times for 8 players
  { 0x03318, 0x00000000, 0x38670000 },  // cave: restore times for 8 players
  { 0x0331C, 0x00000000, 0xB068000E },  // cave: restore times for 8 players
  { 0x03320, 0x00000000, 0x807F03A0 },  // cave: restore times for 8 players
  { 0x03324, 0x00000000, 0x906C0390 },  // cave: restore times for 8 players
  { 0x03328, 0x00000000, 0x38600000 },  // cave: restore times for 8 players
  { 0x0332C, 0x00000000, 0x90680010 },  // cave: restore times for 8 players
  { 0x03330, 0x00000000, 0x90680014 },  // cave: restore times for 8 players
  { 0x03334, 0x00000000, 0x90680018 },  // cave: restore times for 8 players
  { 0x03338, 0x00000000, 0x9068001C },  // cave: restore times for 8 players
  { 0x0333C, 0x00000000, 0x4805720C },  // cave: restore times for 8 players
  { 0x5A51C, 0xB06DE2B0, 0x4BFA8D94 },  // restore times for 8 players
  { 0x03340, 0x00000000, 0x380018FF },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03344, 0x00000000, 0x390DEE00 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03348, 0x00000000, 0x39200008 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x0334C, 0x00000000, 0xA0680000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03350, 0x00000000, 0xA0E80010 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03354, 0x00000000, 0x2C030000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03358, 0x00000000, 0x41820008 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x0335C, 0x00000000, 0x7C633A14 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03360, 0x00000000, 0x7C030000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03364, 0x00000000, 0x40810008 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03368, 0x00000000, 0x7C030378 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x0336C, 0x00000000, 0xB0680000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03370, 0x00000000, 0x39080002 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03374, 0x00000000, 0x3529FFFF },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03378, 0x00000000, 0x4082FFD4 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x0337C, 0x00000000, 0x38600000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03380, 0x00000000, 0x390DEE10 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03384, 0x00000000, 0x90680000 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03388, 0x00000000, 0x90680004 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x0338C, 0x00000000, 0x90680008 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03390, 0x00000000, 0x9068000C },  // cave: add time bonus for 8 players, clear bonuses
  { 0x03394, 0x00000000, 0x48058394 },  // cave: add time bonus for 8 players, clear bonuses
  { 0x5B680, 0x380018FF, 0x4BFA7CC0 },  // add time bonus for 8 players, clear bonuses
  { 0x5B714, 0x38600000, 0x4BFA7C68 },  // bonus clear path: clear 8 bonuses
  { 0x03398, 0x00000000, 0x390DEE00 },  // cave: set time/bonus for 8 players
  { 0x0339C, 0x00000000, 0xB2480000 },  // cave: set time/bonus for 8 players
  { 0x033A0, 0x00000000, 0xB2480002 },  // cave: set time/bonus for 8 players
  { 0x033A4, 0x00000000, 0xB2480004 },  // cave: set time/bonus for 8 players
  { 0x033A8, 0x00000000, 0xB2480006 },  // cave: set time/bonus for 8 players
  { 0x033AC, 0x00000000, 0xB2480008 },  // cave: set time/bonus for 8 players
  { 0x033B0, 0x00000000, 0xB248000A },  // cave: set time/bonus for 8 players
  { 0x033B4, 0x00000000, 0xB248000C },  // cave: set time/bonus for 8 players
  { 0x033B8, 0x00000000, 0xB248000E },  // cave: set time/bonus for 8 players
  { 0x033BC, 0x00000000, 0xB0880010 },  // cave: set time/bonus for 8 players
  { 0x033C0, 0x00000000, 0xB0880012 },  // cave: set time/bonus for 8 players
  { 0x033C4, 0x00000000, 0xB0880014 },  // cave: set time/bonus for 8 players
  { 0x033C8, 0x00000000, 0xB0880016 },  // cave: set time/bonus for 8 players
  { 0x033CC, 0x00000000, 0xB0880018 },  // cave: set time/bonus for 8 players
  { 0x033D0, 0x00000000, 0xB088001A },  // cave: set time/bonus for 8 players
  { 0x033D4, 0x00000000, 0xB088001C },  // cave: set time/bonus for 8 players
  { 0x033D8, 0x00000000, 0xB088001E },  // cave: set time/bonus for 8 players
  { 0x033DC, 0x00000000, 0x4E800020 },  // cave: set time/bonus for 8 players
  { 0x5CB68, 0xB24DE2B0, 0x4BFA6830 },  // set time/bonus for 8 players
  { 0x033E0, 0x00000000, 0x390DEE10 },  // cave: bonus += r18 for 8 players
  { 0x033E4, 0x00000000, 0x39200008 },  // cave: bonus += r18 for 8 players
  { 0x033E8, 0x00000000, 0xA0A80000 },  // cave: bonus += r18 for 8 players
  { 0x033EC, 0x00000000, 0x7CA59214 },  // cave: bonus += r18 for 8 players
  { 0x033F0, 0x00000000, 0xB0A80000 },  // cave: bonus += r18 for 8 players
  { 0x033F4, 0x00000000, 0x39080002 },  // cave: bonus += r18 for 8 players
  { 0x033F8, 0x00000000, 0x3529FFFF },  // cave: bonus += r18 for 8 players
  { 0x033FC, 0x00000000, 0x4082FFEC },  // cave: bonus += r18 for 8 players
  { 0x03400, 0x00000000, 0x4E800020 },  // cave: bonus += r18 for 8 players
  { 0x5CBA0, 0xA0ADE2B8, 0x4BFA6840 },  // bonus += r18 for 8 players
  { 0x03404, 0x00000000, 0xB0E3022E },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03408, 0x00000000, 0x888D865B },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x0340C, 0x00000000, 0x2C040005 },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03410, 0x00000000, 0x41800020 },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03414, 0x00000000, 0x3884FFFF },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03418, 0x00000000, 0x5485083C },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x0341C, 0x00000000, 0x38CDEE00 },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03420, 0x00000000, 0x7CE62A2E },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03424, 0x00000000, 0x54850F7C },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03428, 0x00000000, 0x38A50228 },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x0342C, 0x00000000, 0x7CE32B2E },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x03430, 0x00000000, 0x4806C9D0 },  // cave: packet time: node 5-8 own time in slot (node-1)&3
  { 0x6FDFC, 0xB0E3022E, 0x4BF93608 },  // packet time: node 5-8 own time in slot (node-1)&3
  { 0x03434, 0x00000000, 0x5600077C },  // cave: packet time read: slot idx&3
  { 0x03438, 0x00000000, 0x7C63022E },  // cave: packet time read: slot idx&3
  { 0x0343C, 0x00000000, 0x4806CA10 },  // cave: packet time read: slot idx&3
  { 0x6FE48, 0x7C63822E, 0x4BF935EC },  // packet time read: slot idx&3
  { 0x03440, 0x00000000, 0xB0D1039E },  // cave: save times of players 5-8
  { 0x03444, 0x00000000, 0x390DEE00 },  // cave: save times of players 5-8
  { 0x03448, 0x00000000, 0x3D200016 },  // cave: save times of players 5-8
  { 0x0344C, 0x00000000, 0x61290C20 },  // cave: save times of players 5-8
  { 0x03450, 0x00000000, 0x80680008 },  // cave: save times of players 5-8
  { 0x03454, 0x00000000, 0x90690000 },  // cave: save times of players 5-8
  { 0x03458, 0x00000000, 0x8068000C },  // cave: save times of players 5-8
  { 0x0345C, 0x00000000, 0x90690004 },  // cave: save times of players 5-8
  { 0x03460, 0x00000000, 0x4806F478 },  // cave: save times of players 5-8
  { 0x728D4, 0xB0D1039E, 0x4BF90B6C },  // save times of players 5-8
  { 0x50E90, 0x386DC07C, 0x386DEE20 },  // player colour array -> 8 entries (0x106E20)
  { 0x5E840, 0x38CDC07C, 0x38CDEE20 },  // player colour array -> 8 entries (0x106E20)
  { 0x782DC, 0x3B0DC07C, 0x3B0DEE20 },  // player colour array -> 8 entries (0x106E20)
  { 0x03464, 0x00000000, 0x3C600016 },  // cave: restore colours of 8 players
  { 0x03468, 0x00000000, 0x80630C40 },  // cave: restore colours of 8 players
  { 0x0346C, 0x00000000, 0x906DEE24 },  // cave: restore colours of 8 players
  { 0x03470, 0x00000000, 0x807F03E0 },  // cave: restore colours of 8 players
  { 0x03474, 0x00000000, 0x906DEE20 },  // cave: restore colours of 8 players
  { 0x03478, 0x00000000, 0x48056F64 },  // cave: restore colours of 8 players
  { 0x5A3D4, 0x807F03E0, 0x4BFA9090 },  // restore colours of 8 players
  { 0x0347C, 0x00000000, 0x800DEE24 },  // cave: save colours of 8 players
  { 0x03480, 0x00000000, 0x3C600016 },  // cave: save colours of 8 players
  { 0x03484, 0x00000000, 0x90030C40 },  // cave: save colours of 8 players
  { 0x03488, 0x00000000, 0x806DEE20 },  // cave: save colours of 8 players
  { 0x0348C, 0x00000000, 0x907103E0 },  // cave: save colours of 8 players
  { 0x03490, 0x00000000, 0x4806F3D4 },  // cave: save colours of 8 players
  { 0x7285C, 0x806DC07C, 0x4BF90C20 },  // save colours of 8 players
  { 0x6EDE0, 0x777B0FFF, 0x777B00FF },  // in-game mask on leave: clear node bits of 8 nodes
  { 0x550A4, 0x2C830004, 0x2C830008 },  // reference angles from owner packet for 8 players
  { 0x03494, 0x00000000, 0x38600000 },  // cave: costume colour 0-7 (11/14 col8): li r3, 0
  { 0x03498, 0x00000000, 0x9461FFFC },  // cave: costume colour 0-7 (11/14 col8): stwu r3, -4(r1)
  { 0x0349C, 0x00000000, 0x9461FFFC },  // cave: costume colour 0-7 (11/14 col8): stwu r3, -4(r1)
  { 0x034A0, 0x00000000, 0x3CE00014 },  // cave: costume colour 0-7 (11/14 col8): lis r7, 0x14
  { 0x034A4, 0x00000000, 0x82070128 },  // cave: costume colour 0-7 (11/14 col8): lwz r16, 0x128(r7)
  { 0x034A8, 0x00000000, 0x7E120034 },  // cave: costume colour 0-7 (11/14 col8): cntlzw r18, r16
  { 0x034AC, 0x00000000, 0x3C408000 },  // cave: costume colour 0-7 (11/14 col8): lis r2, -0x8000
  { 0x034B0, 0x00000000, 0x7C429430 },  // cave: costume colour 0-7 (11/14 col8): srw r2, r2, r18
  { 0x034B4, 0x00000000, 0x7E101078 },  // cave: costume colour 0-7 (11/14 col8): andc r16, r16, r2
  { 0x034B8, 0x00000000, 0x3A520001 },  // cave: costume colour 0-7 (11/14 col8): addi r18, r18, 1
  { 0x034BC, 0x00000000, 0x7C12A800 },  // cave: costume colour 0-7 (11/14 col8): cmpw r18, r21
  { 0x034C0, 0x00000000, 0x4181003C },  // cave: costume colour 0-7 (11/14 col8): bgt 0x34fc
  { 0x034C4, 0x00000000, 0x5642103A },  // cave: costume colour 0-7 (11/14 col8): slwi r2, r18, 2
  { 0x034C8, 0x00000000, 0x3C420015 },  // cave: costume colour 0-7 (11/14 col8): addis r2, r2, 0x15
  { 0x034CC, 0x00000000, 0x80620060 },  // cave: costume colour 0-7 (11/14 col8): lwz r3, 0x60(r2)
  { 0x034D0, 0x00000000, 0x88830025 },  // cave: costume colour 0-7 (11/14 col8): lbz r4, 0x25(r3)
  { 0x034D4, 0x00000000, 0x7C049800 },  // cave: costume colour 0-7 (11/14 col8): cmpw r4, r19
  { 0x034D8, 0x00000000, 0x4082FFD0 },  // cave: costume colour 0-7 (11/14 col8): bne 0x34a8
  { 0x034DC, 0x00000000, 0x7C7790AE },  // cave: costume colour 0-7 (11/14 col8): lbzx r3, r23, r18
  { 0x034E0, 0x00000000, 0x2C0300FF },  // cave: costume colour 0-7 (11/14 col8): cmpwi r3, 0xff
  { 0x034E4, 0x00000000, 0x4182FFC4 },  // cave: costume colour 0-7 (11/14 col8): beq 0x34a8
  { 0x034E8, 0x00000000, 0x5463077E },  // cave: costume colour 0-7 (11/14 col8): clrlwi r3, r3, 0x1d
  { 0x034EC, 0x00000000, 0x7C8118AE },  // cave: costume colour 0-7 (11/14 col8): lbzx r4, r1, r3
  { 0x034F0, 0x00000000, 0x38840001 },  // cave: costume colour 0-7 (11/14 col8): addi r4, r4, 1
  { 0x034F4, 0x00000000, 0x7C8119AE },  // cave: costume colour 0-7 (11/14 col8): stbx r4, r1, r3
  { 0x034F8, 0x00000000, 0x4BFFFFB0 },  // cave: costume colour 0-7 (11/14 col8): b 0x34a8
  { 0x034FC, 0x00000000, 0x38A00000 },  // cave: costume colour 0-7 (11/14 col8): li r5, 0
  { 0x03500, 0x00000000, 0x3A400000 },  // cave: costume colour 0-7 (11/14 col8): li r18, 0
  { 0x03504, 0x00000000, 0x7C8190AE },  // cave: costume colour 0-7 (11/14 col8): lbzx r4, r1, r18
  { 0x03508, 0x00000000, 0x7C042800 },  // cave: costume colour 0-7 (11/14 col8): cmpw r4, r5
  { 0x0350C, 0x00000000, 0x41810010 },  // cave: costume colour 0-7 (11/14 col8): bgt 0x351c
  { 0x03510, 0x00000000, 0x2C140000 },  // cave: costume colour 0-7 (11/14 col8): cmpwi r20, 0
  { 0x03514, 0x00000000, 0x418208B4 },  // cave: costume colour 0-7 (11/14 col8): beq 0x3dc8
  { 0x03518, 0x00000000, 0x3A94FFFF },  // cave: costume colour 0-7 (11/14 col8): addi r20, r20, -1
  { 0x0351C, 0x00000000, 0x3A520001 },  // cave: costume colour 0-7 (11/14 col8): addi r18, r18, 1
  { 0x03520, 0x00000000, 0x2C120008 },  // cave: costume colour 0-7 (11/14 col8): cmpwi r18, 8
  { 0x03524, 0x00000000, 0x4180FFE0 },  // cave: costume colour 0-7 (11/14 col8): blt 0x3504
  { 0x03528, 0x00000000, 0x38A50001 },  // cave: costume colour 0-7 (11/14 col8): addi r5, r5, 1
  { 0x0352C, 0x00000000, 0x4BFFFFD4 },  // cave: costume colour 0-7 (11/14 col8): b 0x3500
  { 0x03530, 0x00000000, 0x60000000 },  // cave: costume colour 0-7 (11/14 col8): nop
  { 0x03534, 0x00000000, 0x60000000 },  // cave: costume colour 0-7 (11/14 col8): nop
  { 0x03538, 0x00000000, 0x60000000 },  // cave: costume colour 0-7 (11/14 col8): nop
  // 11/14 (user) col8: same-character colours go 0-7 = red blue green orange pink purple black white in pick order, any slot.
  // The ROM costume table has 4 colours: it is read with colour & 3; the emulator swaps colours 4-7 to the new ones (Real3D).
  // A new pick clears LASTTEX[slot] (0x3C24) so the costume is sent again even when colour & 3 is unchanged.
  { 0x03DC8, 0x00000000, 0x38000000 },  // cave col8: li r0, 0
  { 0x03DCC, 0x00000000, 0x56C2103A },  // cave col8: slwi r2, r22, 2
  { 0x03DD0, 0x00000000, 0x90023C20 },  // cave col8: stw r0, 0x3c20(r2)
  { 0x03DD4, 0x00000000, 0x38720000 },  // cave col8: addi r3, r18, 0
  { 0x03DD8, 0x00000000, 0x4806F838 },  // cave col8: b 0x73610
  { 0x5B3F0, 0x5463103A, 0x5463173A },  // col8: costume table index = colour & 3 (rlwinm r3,r3,2,28,29)
  { 0x7343C, 0x5463103A, 0x5463173A },  // col8: costume table index = colour & 3 (join)
  { 0x73614, 0x38210004, 0x38210008 },  // col8: free the 8 colour count bytes
  { 0x73564, 0x3821FFFC, 0x4BF8FF30 },  // costume = same-character count, wraps 0-3, over 8 players
  { 0x0353C, 0x00000000, 0x3880FFFF },  // cave: costume array reset for 8 players
  { 0x03540, 0x00000000, 0x908300A4 },  // cave: costume array reset for 8 players
  { 0x03544, 0x00000000, 0x480472C0 },  // cave: costume array reset for 8 players
  { 0x4A800, 0x3880FFFF, 0x4BFB8D3C },  // costume array reset for 8 players
  { 0x53838, 0x5463083C, 0x54630F7C },  // player marker model: slot&3
  { 0x03548, 0x00000000, 0x000A1924 },  // data: texture upload areas for 8 player slots: slot 0
  { 0x0354C, 0x00000000, 0x000A194C },  // data: texture upload areas for 8 player slots: slot 1
  { 0x03550, 0x00000000, 0x000A1974 },  // data: texture upload areas for 8 player slots: slot 2
  { 0x03554, 0x00000000, 0x000A199C },  // data: texture upload areas for 8 player slots: slot 3
  { 0x03558, 0x00000000, 0x00003568 },  // data: texture upload areas for 8 player slots: slot 4 (Page 1 Row 3 Col 1)
  { 0x0355C, 0x00000000, 0x00003590 },  // data: texture upload areas for 8 player slots: slot 5 (Page 1 Row 3 Col 2)
  { 0x03560, 0x00000000, 0x000035B8 },  // data: texture upload areas for 8 player slots: slot 6 (Page 1 Row 2 Col 1)
  { 0x03564, 0x00000000, 0x000035E0 },  // data: texture upload areas for 8 player slots: slot 7 (Page 1 Row 2 Col 2)
  { 0x03568, 0x00000000, 0x00F48C20 },  // data: slot 4 desc block 0 (Page 1 Row 3, xb=0x20, yb=0x18)
  { 0x0356C, 0x00000000, 0x00F48E20 },  // data: slot 4 desc block 1
  { 0x03570, 0x00000000, 0x00F48C24 },  // data: slot 4 desc block 2
  { 0x03574, 0x00000000, 0x00F48E24 },  // data: slot 4 desc block 3
  { 0x03578, 0x00000000, 0x00F48C28 },  // data: slot 4 desc block 4
  { 0x0357C, 0x00000000, 0x00F48E28 },  // data: slot 4 desc block 5
  { 0x03580, 0x00000000, 0x00F48C2C },  // data: slot 4 desc block 6
  { 0x03584, 0x00000000, 0x00F48E2C },  // data: slot 4 desc block 7
  { 0x03588, 0x00000000, 0x00F48C30 },  // data: slot 4 desc block 8
  { 0x0358C, 0x00000000, 0x00F48E30 },  // data: slot 4 desc block 9
  { 0x03590, 0x00000000, 0x00F48820 },  // data: slot 5 desc block 0 (Page 1 Row 2, xb=0x20, yb=0x10)
  { 0x03594, 0x00000000, 0x00F48A20 },  // data: slot 5 desc block 1
  { 0x03598, 0x00000000, 0x00F48824 },  // data: slot 5 desc block 2
  { 0x0359C, 0x00000000, 0x00F48A24 },  // data: slot 5 desc block 3
  { 0x035A0, 0x00000000, 0x00F48828 },  // data: slot 5 desc block 4
  { 0x035A4, 0x00000000, 0x00F48A28 },  // data: slot 5 desc block 5
  { 0x035A8, 0x00000000, 0x00F4882C },  // data: slot 5 desc block 6
  { 0x035AC, 0x00000000, 0x00F48A2C },  // data: slot 5 desc block 7
  { 0x035B0, 0x00000000, 0x00F48830 },  // data: slot 5 desc block 8
  { 0x035B4, 0x00000000, 0x00F48A30 },  // data: slot 5 desc block 9
  { 0x035B8, 0x00000000, 0x00E48C20 },  // data: slot 6 desc block 0 (Page 0 Row 3, xb=0x20, yb=0x18)
  { 0x035BC, 0x00000000, 0x00E48E20 },  // data: slot 6 desc block 1
  { 0x035C0, 0x00000000, 0x00E48C24 },  // data: slot 6 desc block 2
  { 0x035C4, 0x00000000, 0x00E48E24 },  // data: slot 6 desc block 3
  { 0x035C8, 0x00000000, 0x00E48C28 },  // data: slot 6 desc block 4
  { 0x035CC, 0x00000000, 0x00E48E28 },  // data: slot 6 desc block 5
  { 0x035D0, 0x00000000, 0x00E48C2C },  // data: slot 6 desc block 6
  { 0x035D4, 0x00000000, 0x00E48E2C },  // data: slot 6 desc block 7
  { 0x035D8, 0x00000000, 0x00E48C30 },  // data: slot 6 desc block 8
  { 0x035DC, 0x00000000, 0x00E48E30 },  // data: slot 6 desc block 9
  { 0x035E0, 0x00000000, 0x00E48820 },  // data: slot 7 desc block 0 (Page 0 Row 2, xb=0x20, yb=0x10)
  { 0x035E4, 0x00000000, 0x00E48A20 },  // data: slot 7 desc block 1
  { 0x035E8, 0x00000000, 0x00E48824 },  // data: slot 7 desc block 2
  { 0x035EC, 0x00000000, 0x00E48A24 },  // data: slot 7 desc block 3
  { 0x035F0, 0x00000000, 0x00E48828 },  // data: slot 7 desc block 4
  { 0x035F4, 0x00000000, 0x00E48A28 },  // data: slot 7 desc block 5
  { 0x035F8, 0x00000000, 0x00E4882C },  // data: slot 7 desc block 6
  { 0x035FC, 0x00000000, 0x00E48A2C },  // data: slot 7 desc block 7
  { 0x03600, 0x00000000, 0x00E48830 },  // data: slot 7 desc block 8
  { 0x03604, 0x00000000, 0x00E48A30 },  // data: slot 7 desc block 9
  { 0x581A8, 0x3C60000A, 0x3C600000 },  // costume texture upload: 8-slot area table
  { 0x581AC, 0x38631914, 0x38633548 },  // costume texture upload: 8-slot area table
  { 0x03608, 0x00000000, 0x00001400 },  // data: player texture offset for 8 slots: slot 0 (00,00), slot 1 (14,00)
  { 0x0360C, 0x00000000, 0x28000018 },  // data: player texture offset for 8 slots: slot 2 (28,00), slot 3 (00,18)
  { 0x03610, 0x00000000, 0x20182010 },  // data: player texture offset for 8 slots: slot 4 (20,18), slot 5 (20,10)
  { 0x03614, 0x00000000, 0x20182010 },  // data: player texture offset for 8 slots: slot 6 (20,18, Page 0), slot 7 (20,10, Page 0)
  { 0x34EF8, 0x3C800009, 0x3C800000 },  // player texture offset: 8-slot table
  { 0x34EFC, 0x38841B96, 0x38843608 },  // player texture offset: 8-slot table
  { 0x36134, 0x3C800009, 0x3C800000 },  // player texture offset: 8-slot table
  { 0x36138, 0x38841B96, 0x38843608 },  // player texture offset: 8-slot table
  { 0x34680, 0x2C020004, 0x2C020008 },  // player 0-7 vs enemy Rob (cmpwi r2, 8)
  { 0x34690, 0x3842FFFC, 0x3842FFF8 },  // enemy Rob offset (r2 - 8)
  { 0x34694, 0x3C00001B, 0x3C00001C },  // enemy Rob base 0x1C0000
  { 0x34F34, 0x50C2067E, 0x4BFCEA1C },  // player texture offset: hook cave A at 0x03950
  { 0x36170, 0x50C2067E, 0x4BFCD810 },  // player texture offset: hook cave B at 0x03980
  { 0x03950, 0x00000000, 0x50C2067E },  // cave A: rlwimi r2, r6, 0, 25, 31 (original y-offset)
  { 0x03954, 0x00000000, 0x544204A0 },  // cave A: rlwinm r2, r2, 0, 18, 16 (clear bit 14 -> Page 1)
  { 0x03958, 0x00000000, 0x3C00001C },  // cave A: lis r0, 0x1C (r0 = 0x001C0000 enemy base)
  { 0x0395C, 0x00000000, 0x7C0E0040 },  // cave A: cmplw r14, r0 (compare actor pointer)
  { 0x03960, 0x00000000, 0x4080001C },  // cave A: bge +28 (r14 >= 0x1C0000 -> enemy, stay on Page 1)
  { 0x03964, 0x00000000, 0x880E0004 },  // cave A: lbz r0, 4(r14) (player slot number 0-7)
  { 0x03968, 0x00000000, 0x2C000006 },  // cave A: cmpwi r0, 6
  { 0x0396C, 0x00000000, 0x41800010 },  // cave A: blt +16 (slot < 6 -> stay on Page 1)
  { 0x03970, 0x00000000, 0x2C000007 },  // cave A: cmpwi r0, 7
  { 0x03974, 0x00000000, 0x41810008 },  // cave A: bgt +8 (slot > 7 -> stay on Page 1)
  { 0x03978, 0x00000000, 0x60424000 },  // cave A: ori r2, r2, 0x4000 (slot 6,7 -> Page 0)
  { 0x0397C, 0x00000000, 0x480315BC },  // cave A: b 0x34F38
  { 0x03980, 0x00000000, 0x50C2067E },  // cave B: rlwimi r2, r6, 0, 25, 31 (original y-offset)
  { 0x03984, 0x00000000, 0x544204A0 },  // cave B: rlwinm r2, r2, 0, 18, 16 (clear bit 14 -> Page 1)
  { 0x03988, 0x00000000, 0x3C00001C },  // cave B: lis r0, 0x1C (r0 = 0x001C0000 enemy base)
  { 0x0398C, 0x00000000, 0x7C0E0040 },  // cave B: cmplw r14, r0 (compare actor pointer)
  { 0x03990, 0x00000000, 0x4080001C },  // cave B: bge +28 (r14 >= 0x1C0000 -> enemy, stay on Page 1)
  { 0x03994, 0x00000000, 0x880E0004 },  // cave B: lbz r0, 4(r14) (player slot number 0-7)
  { 0x03998, 0x00000000, 0x2C000006 },  // cave B: cmpwi r0, 6
  { 0x0399C, 0x00000000, 0x41800010 },  // cave B: blt +16 (slot < 6 -> stay on Page 1)
  { 0x039A0, 0x00000000, 0x2C000007 },  // cave B: cmpwi r0, 7
  { 0x039A4, 0x00000000, 0x41810008 },  // cave B: bgt +8 (slot > 7 -> stay on Page 1)
  { 0x039A8, 0x00000000, 0x60424000 },  // cave B: ori r2, r2, 0x4000 (slot 6,7 -> Page 0)
  { 0x039AC, 0x00000000, 0x480327C8 },  // cave B: b 0x36174
  // --- Costume upload de-dup per player slot (11/11) ---
  // The texture request queue (fa_tex +0xA0, built at 0x58254/0x586BC) has only 8 nodes and 0x5870C silently
  // drops a request when it is full. A mid-join re-uploads every in-game player's costume (0x733C8): 7-8
  // requests filled the queue, the enemy-cache uploads that follow (0x580B0) were dropped while the cache
  // table still said "loaded" -> black enemies on every cabinet until the next area. Every costume upload now
  // goes through a cave that skips it when that slot was last sent the same texture source (LASTTEX[slot] at
  // 0x3C24, 8 words, node-local, starts 0): a join only uploads the newcomer's costume.
  // (The shared-per-character costume version of 11/11 was reverted at the user's request.)
  { 0x73450, 0x4BFE4D05, 0x4BF907F5 },  // costume upload (join/restore, all players) -> dedup cave
  { 0x5B404, 0x4BFFCD51, 0x4BFA8841 },  // costume upload (one player) -> dedup cave
  { 0x51594, 0x48006BC1, 0x4BFB26B1 },  // actor-callback costume upload -> dedup cave
  { 0x6A3A4, 0x4BFEDDB1, 0x4BF998A1 },  // special slot-1 upload -> dedup cave
  { 0x03C44, 0x00000000, 0x5487103A },  // cave dedup: slwi r7, r4, 2 (slot * 4)
  { 0x03C48, 0x00000000, 0x80C73C24 },  // cave dedup: lwz r6, LASTTEX(r7)
  { 0x03C4C, 0x00000000, 0x7C061800 },  // cave dedup: cmpw r6, r3 (same texture source?)
  { 0x03C50, 0x00000000, 0x4D820020 },  // cave dedup: beqlr (already in that slot: no upload)
  { 0x03C54, 0x00000000, 0x90673C24 },  // cave dedup: stw r3, LASTTEX(r7)
  { 0x03C58, 0x00000000, 0x480544FC },  // cave dedup: b 0x58154
  // --- Mid-join save/restore: enemy 0-3 = players 5-8 (11/11) ---
  // Enemy k lives at 0x1B0000 + k*0x4000; the 8-player layout gives k = 0-3 (0x1B0000-0x1BFFFF) to players 5-8
  // and the enemy allocators skip k = 0-3. The mid-join save (0x72684) still built its 16-enemy alive mask from
  // k = 0, so active players 5-8 were saved as enemies 0-3; the restore then created enemies on top of them
  // (0x5A2FC -> 0x51F58) and wrote their +0x3F00 combat block back from the snapshot (0x5A328). Only bit when
  // players 5-8 were already playing, i.e. the 8th player joining a running 7-player game.
  { 0x72D68, 0x90700004, 0x4BF90F20 },  // join save: enemy alive mask -> cave (drop k 0-3)
  { 0x03C88, 0x00000000, 0x5463013E },  // cave: clrlwi r3, r3, 4 (enemy 0-3 = players 5-8, never saved as enemies)
  { 0x03C8C, 0x00000000, 0x90700004 },  // cave: stw r3, 4(r16)
  { 0x03C90, 0x00000000, 0x4806F0DC },  // cave: b 0x72D6C
  { 0x5A328, 0x3B5F0628, 0x3B5F0A28 },  // join restore: enemy +0x3F00 blocks from k = 4 (save area +0x400)
  { 0x5A32C, 0x3F80001B, 0x3F80001C },  // join restore: enemy +0x3F00 blocks from k = 4 (Rob 0x1C0000)
  { 0x5A354, 0x2C1B0010, 0x2C1B000C },  // join restore: 12 enemies (k 4-15)
  { 0x732F0, 0x3EC0001B, 0x3EC0001C },  // gameplay init: clear enemy +0x3F00 from k = 4 (not players 5-8)
  { 0x73314, 0x2C120010, 0x2C12000C },  // gameplay init: 12 enemies (k 4-15)
  { 0x03630, 0x00000000, 0x80910000 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03634, 0x00000000, 0x70800002 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03638, 0x00000000, 0x41820020 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x0363C, 0x00000000, 0x3C400014 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03640, 0x00000000, 0x80420128 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03644, 0x00000000, 0x3C008000 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03648, 0x00000000, 0x7C009430 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x0364C, 0x00000000, 0x7C001039 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03650, 0x00000000, 0x40820008 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03654, 0x00000000, 0x38800000 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x03658, 0x00000000, 0x4806F550 },  // cave: area save: enemy in an idle player slot is not a player
  { 0x72BA4, 0x80910000, 0x4BF90A8C },  // area save: enemy in an idle player slot is not a player
  { 0x69CAC, 0x38800000, 0x38800004 },  // cut-scene enemy list: first enemy index 4 (ID 8)
  { 0x69F60, 0x38800000, 0x38800004 },  // cut-scene enemy list: first enemy index 4 (ID 8)
  { 0x6AC64, 0x38800000, 0x38800004 },  // cut-scene enemy list: first enemy index 4 (ID 8)
  { 0x6AEC8, 0x38800000, 0x38800004 },  // cut-scene enemy list: first enemy index 4 (ID 8)
  { 0x6B1FC, 0x38800000, 0x38800004 },  // cut-scene enemy list: first enemy index 4 (ID 8)
  { 0x0365C, 0x00000000, 0x80810000 },  // cave: cut-scene cmd 5: enemy index +4
  { 0x03660, 0x00000000, 0x38840004 },  // cave: cut-scene cmd 5: enemy index +4
  { 0x03664, 0x00000000, 0x4806710C },  // cave: cut-scene cmd 5: enemy index +4
  { 0x6A76C, 0x80810000, 0x4BF98EF0 },  // cut-scene cmd 5: enemy index +4
  { 0x6A554, 0x3C00001B, 0x3C00001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A5A0, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A5CC, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A5E0, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A62C, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A664, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A6A4, 0x3DC0001B, 0x3DC0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A6A8, 0x3DE0001B, 0x3DE0001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6AD60, 0x3C60001B, 0x3C60001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6B0A8, 0x3C60001B, 0x3C60001C },  // cut-scene: enemy Rob ID 4-7 -> 8-11
  { 0x6A87C, 0x3DC0001C, 0x3DC0001D },  // cut-scene cmd 36: enemy Rob ID 6 -> 10
  { 0x03668, 0x00000000, 0x3884FFFF },  // cave: player count for enemy sets: at most 4
  { 0x0366C, 0x00000000, 0x2C040004 },  // cave: player count for enemy sets: at most 4
  { 0x03670, 0x00000000, 0x40810008 },  // cave: player count for enemy sets: at most 4
  { 0x03674, 0x00000000, 0x38800004 },  // cave: player count for enemy sets: at most 4
  { 0x03678, 0x00000000, 0x4806A3B4 },  // cave: player count for enemy sets: at most 4
  { 0x6DA28, 0x3884FFFF, 0x4BF95C40 },  // player count for enemy sets: at most 4
  { 0x4F0E0, 0x38A3031C, 0x38ADEEC0 },  // respawn array B -> 8 entries (0x106EC0)
  { 0x5AB58, 0x38AC02DC, 0x38ADEE40 },  // respawn array A -> 8 entries (0x106E40)
  { 0x5ABA8, 0x2C040004, 0x2C040008 },  // checkpoint update: 8 players
  { 0x5B320, 0x38AC02DC, 0x38ADEE40 },  // respawn array A -> 8 entries (0x106E40)
  { 0x5D51C, 0x388C02DC, 0x388DEE40 },  // area entry: respawn array A -> 8 entries
  { 0x5D520, 0x390C031C, 0x390DEEC0 },  // area entry: respawn array B -> 8 entries
  { 0x0367C, 0x00000000, 0x546507BE },  // cave: area entry: respawn start record of slot&3
  { 0x03680, 0x00000000, 0x1CA50028 },  // cave: area entry: respawn start record of slot&3
  { 0x03684, 0x00000000, 0x48059EA4 },  // cave: area entry: respawn start record of slot&3
  { 0x5D524, 0x1CA30028, 0x4BFA6158 },  // area entry: respawn start record of slot&3
  { 0x5D574, 0x2C030004, 0x2C030008 },  // area entry: respawn positions for 8 players
  { 0x5E864, 0x388C02DC, 0x388DEE40 },  // area entry: respawn array A -> 8 entries
  { 0x5E868, 0x390C031C, 0x390DEEC0 },  // area entry: respawn array B -> 8 entries
  { 0x03688, 0x00000000, 0x546607BE },  // cave: area entry: respawn start record of slot&3
  { 0x0368C, 0x00000000, 0x1CC60028 },  // cave: area entry: respawn start record of slot&3
  { 0x03690, 0x00000000, 0x4805B1E0 },  // cave: area entry: respawn start record of slot&3
  { 0x5E86C, 0x1CC30028, 0x4BFA4E1C },  // area entry: respawn start record of slot&3
  { 0x5E8D0, 0x2C030004, 0x2C030008 },  // area entry: respawn positions for 8 players
  { 0x4A8E0, 0x5083456E, 0x5083452E },  // node sequence packing: include bit 3 for node 8 (rlwimi r3,r4,8,20,23)
  { 0x03694, 0x00000000, 0x3CA00016 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x03698, 0x00000000, 0x60A50C80 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x0369C, 0x00000000, 0x906DC00C },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036A0, 0x00000000, 0x90850000 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036A4, 0x00000000, 0x90850004 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036A8, 0x00000000, 0x90850008 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036AC, 0x00000000, 0x9085000C },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036B0, 0x00000000, 0x90850010 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036B4, 0x00000000, 0x90850014 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036B8, 0x00000000, 0x90850018 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036BC, 0x00000000, 0x9085001C },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x036C0, 0x00000000, 0x48047618 },  // cave: submode 2: clear 8-entry selection table at 0x160C80
  { 0x4ACC0, 0x38ADE268, 0x4BFB89D4 },  // submode 2: clear 8-entry selection table at 0x160C80
  { 0x036C4, 0x00000000, 0x3CE00016 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036C8, 0x00000000, 0x60E70C80 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036CC, 0x00000000, 0x90670000 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036D0, 0x00000000, 0x90670004 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036D4, 0x00000000, 0x90670008 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036D8, 0x00000000, 0x9067000C },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036DC, 0x00000000, 0x90670010 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036E0, 0x00000000, 0x90670014 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036E4, 0x00000000, 0x90670018 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036E8, 0x00000000, 0x9067001C },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x036EC, 0x00000000, 0x48047654 },  // cave: submode 3: clear 8-entry selection table at 0x160C80
  { 0x4AD2C, 0x38EDE268, 0x4BFB8998 },  // submode 3: clear 8-entry selection table at 0x160C80
  { 0x036F0, 0x00000000, 0x3E800016 },  // cave: route voter: base address -> 0x160C80
  { 0x036F4, 0x00000000, 0x62940C80 },  // cave: route voter: base address -> 0x160C80
  { 0x036F8, 0x00000000, 0x480708C4 },  // cave: route voter: base address -> 0x160C80
  { 0x73FB8, 0x3A8DE268, 0x4BF8FB08 },  // route voter: base address -> 0x160C80 + clear 8 candidates
  { 0x7333C, 0x2C120004, 0x2C120008 },  // gameplay init: clear 8 player rob+0x3f00
  { 0x036FC, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03700, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03704, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03708, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x0370C, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03710, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03714, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x03718, 0x00000000, 0xB4834000 },  // cave: mode 4: clear invincibility for 8 players
  { 0x0371C, 0x00000000, 0x48047070 },  // cave: mode 4: clear invincibility for 8 players
  { 0x4A780, 0xB4834000, 0x4BFB8F7C },  // mode 4: clear invincibility for 8 players
  { 0x5DC60, 0x2C030004, 0x2C030008 },  // per-player script setter loop: 8 players (special stock etc.)
  { 0x5D660, 0x2C840004, 0x2C840008 },  // 11/12 script cmd 0x5D64C (event start): lock input (Rob flag 0x100, read at 0x23FCC) for players 0-7, not 0-3 (slots 4-7 moved in cut scenes)
  { 0x319D4, 0x2C030004, 0x2C030008 },  // 11/14 stage draw (0x30E0C): when the camera's own zone (0x4FDD4 at the camera) won't do (camera above the floor found there, e.g. after a high jump under a ceiling), the zone is taken at the followed player (camera +0x4A, Rob +0x380..+0x388) - only for players 0-3: slots 4-7 kept the camera's zone, the visible-block masks (r30/r29 -> 0x31F9C/0x3212C) were wrong and the floor blocks dropped out of the list (05-08 only)
  { 0x4B90C, 0x40820020, 0x60000000 },  // char select: nop bne to set -0x3F66 for all characters
  { 0x742F4, 0x48000069, 0x60000000 },  // mid-join sync: bypass desync checker bl 0x7435c
  { 0x74300, 0x4182004C, 0x4800004C },  // mid-join sync: bypass communication error abort (b 0x7434c)

  // --- KO/PK counts relocated to 8-entry arrays in the 0x160000 block (sent to mid-join nodes) ---
  { 0x22714, 0x808DC144, 0x3C800016 },  // PK count: lis r4, 0x16 (KO/PK -> 8-entry arrays (KO 0x1601E0, PK 0x1601E8))
  { 0x22718, 0x388400AC, 0x608401E8 },  // PK count: ori r4, r4, 0x1E8 (PK[killer 0-7])
  { 0x22734, 0x808DC144, 0x3C800016 },  // KO count: lis r4, 0x16
  { 0x22738, 0x388400A8, 0x608401E0 },  // KO count: ori r4, r4, 0x1E0 (KO[victim 0-7])
  { 0x5BA24, 0x808DC144, 0x3C800016 },  // KO count (2): lis r4, 0x16
  { 0x5BA28, 0x388400A8, 0x608401E0 },  // KO count (2): ori r4, r4, 0x1E0
  { 0x4B7A8, 0x80ADC144, 0x3CA00016 },  // KO/PK clear (one node): lis r5, 0x16
  { 0x4B7B0, 0x38C500A8, 0x38C501E0 },  // KO/PK clear (one node): addi r6, r5, 0x1E0 (KO)
  { 0x4B7B4, 0x38A500AC, 0x38A501E8 },  // KO/PK clear (one node): addi r5, r5, 0x1E8 (PK)
  { 0x49604, 0x806DC144, 0x4BFBA5FC },  // KO/PK clear (all): b 0x3C00
  { 0x03C00, 0x00000000, 0x806DC144 },  // cave: KO/PK clear (all): lwz r3, -0x3ebc(r13) (keep original r3)
  { 0x03C04, 0x00000000, 0x3CA00016 },  // cave: KO/PK clear (all): lis r5, 0x16
  { 0x03C08, 0x00000000, 0x60A501E0 },  // cave: KO/PK clear (all): ori r5, r5, 0x1E0
  { 0x03C0C, 0x00000000, 0x90850000 },  // cave: KO/PK clear (all): stw r4, 0(r5) (KO 0-3)
  { 0x03C10, 0x00000000, 0x90850004 },  // cave: KO/PK clear (all): stw r4, 4(r5) (KO 4-7)
  { 0x03C14, 0x00000000, 0x90850008 },  // cave: KO/PK clear (all): stw r4, 8(r5) (PK 0-3)
  { 0x03C18, 0x00000000, 0x9085000C },  // cave: KO/PK clear (all): stw r4, 12(r5) (PK 4-7)
  { 0x03C1C, 0x00000000, 0x38C50008 },  // cave: KO/PK clear (all): addi r6, r5, 8
  { 0x03C20, 0x00000000, 0x480459F8 },  // cave: KO/PK clear (all): b 0x49618
  { 0x72808, 0x3C600014, 0x48000054 },  // mid-join save: skip 4-player KO/PK loop (0x160000 block carries 8-player KO/PK)
  { 0x5A454, 0x80EDC144, 0x4800004C },  // mid-join restore: skip 4-player KO/PK restore

  // --- Mid-join State Transfer: include extended 8-player data (0x160000 & 0x106E00) ---
  // New descriptor table placed in cave at 0x03760
  { 0x03760, 0x00000000, 0x00365000 },  // dest: 0x365000 (game state, stage, area)
  { 0x03764, 0x00000000, 0x00000100 },  // size: 0x100 words (0x400 bytes)
  { 0x03768, 0x00000000, 0x00365400 },  // dest: 0x365400
  { 0x0376C, 0x00000000, 0x00000600 },  // size: 0x600 words (0x1800 bytes)
  { 0x03770, 0x00000000, 0x00366C00 },  // dest: 0x366C00
  { 0x03774, 0x00000000, 0x00000600 },  // size: 0x600 words (0x1800 bytes)
  { 0x03778, 0x00000000, 0x00368400 },  // dest: 0x368400
  { 0x0377C, 0x00000000, 0x00000400 },  // size: 0x400 words (0x1000 bytes)
  { 0x03780, 0x00000000, 0x00369400 },  // dest: 0x369400
  { 0x03784, 0x00000000, 0x00000400 },  // size: 0x400 words (0x1000 bytes)
  { 0x03788, 0x00000000, 0x00160000 },  // dest: 0x160000 (8-player records, blocks, spills, tables)
  { 0x0378C, 0x00000000, 0x00000340 },  // size: 0x340 words (0xD00 bytes)
  { 0x03790, 0x00000000, 0x00106E00 },  // dest: 0x106E00 (8-player time, color, respawn arrays)
  { 0x03794, 0x00000000, 0x00000040 },  // size: 0x040 words (0x100 bytes)
  { 0x03798, 0x00000000, 0xFFFFFFFF },  // terminator
  { 0x0379C, 0x00000000, 0x00000000 },
  { 0x6E454, 0x3C60000B, 0x3C600000 },  // state transfer descriptor: lis r3, 0
  { 0x6E458, 0x38631AB0, 0x38633760 },  // state transfer descriptor: addi r3, r3, 0x3760

  { 0x72164, 0x5C622001, 0x2C040008 },  // player HP=0 death check: cmpwi r4, 8
  { 0x72168, 0x4D820020, 0x40800020 },  // player HP=0 death check: bgelr (return if enemy >= 8; players 0..7 continue)
  { 0x71130, 0x5C622001, 0x2C040008 },  // player knockdown: cmpwi r4, 8
  { 0x71134, 0x4D820020, 0x40800020 },  // player knockdown: bgelr
  { 0x71364, 0x5C622001, 0x2C040008 },  // player recovery: cmpwi r4, 8
  { 0x71368, 0x4D820020, 0x40800020 },  // player recovery: bgelr
  { 0x71C4C, 0x5C622001, 0x2C040008 },  // player continue countdown: cmpwi r4, 8
  { 0x71C50, 0x4D820020, 0x40800020 },  // player continue countdown: bgelr
  { 0x71DDC, 0x5C622001, 0x2C040008 },  // player damage state: cmpwi r4, 8
  { 0x71DE0, 0x4D820020, 0x40800020 },  // player damage state: bgelr
  { 0x2A134, 0x2C040004, 0x2C040008 },  // score multiplier: attacker slot up to 8
  { 0x2A194, 0x2C050004, 0x2C050008 },  // hit score addition: attacker slot up to 8
  { 0x2A4D8, 0x3A000004, 0x3A000008 },  // score update loop: process pending score for 8 players
  { 0x229E0, 0x2C030004, 0x2C030008 },  // enemy killed: killer slot up to 8
  { 0x229EC, 0x2C030004, 0x2C030008 },  // enemy killed bitmask: killer slot up to 8
  { 0x22A2C, 0x388DC0A8, 0x4BFE0D74 },  // enemy killed: hook kill_type to cave
  { 0x22A3C, 0x388DC0A0, 0x4BFE0D88 },  // enemy killed: hook kill counts to cave
  { 0x037A0, 0x00000000, 0x3C800016 },  // cave: enemy kill counts
  { 0x037A4, 0x00000000, 0x60840180 },  // cave: enemy kill counts
  { 0x037A8, 0x00000000, 0x7CA418AE },  // cave: enemy kill counts
  { 0x037AC, 0x00000000, 0x38A50001 },  // cave: enemy kill counts
  { 0x037B0, 0x00000000, 0x7CA419AE },  // cave: enemy kill counts
  { 0x037B4, 0x00000000, 0x2C030004 },  // cave: enemy kill counts
  { 0x037B8, 0x00000000, 0x4080000C },  // cave: enemy kill counts
  { 0x037BC, 0x00000000, 0x388DC0A8 },  // cave: enemy kill counts
  { 0x037C0, 0x00000000, 0x7CA419AE },  // cave: enemy kill counts
  { 0x037C4, 0x00000000, 0x5462083C },  // cave: enemy kill counts
  { 0x037C8, 0x00000000, 0x3C800016 },  // cave: enemy kill counts
  { 0x037CC, 0x00000000, 0x60840150 },  // cave: enemy kill counts
  { 0x037D0, 0x00000000, 0x7CE4122E },  // cave: enemy kill counts
  { 0x037D4, 0x00000000, 0x38E70001 },  // cave: enemy kill counts
  { 0x037D8, 0x00000000, 0x7CE4132E },  // cave: enemy kill counts
  { 0x037DC, 0x00000000, 0x3CA00016 },  // cave: enemy kill counts
  { 0x037E0, 0x00000000, 0x60A50160 },  // cave: enemy kill counts
  { 0x037E4, 0x00000000, 0x7D05122E },  // cave: enemy kill counts
  { 0x037E8, 0x00000000, 0x39080001 },  // cave: enemy kill counts
  { 0x037EC, 0x00000000, 0x7D05132E },  // cave: enemy kill counts
  { 0x037F0, 0x00000000, 0x3CC00016 },  // cave: enemy kill counts
  { 0x037F4, 0x00000000, 0x60C60170 },  // cave: enemy kill counts
  { 0x037F8, 0x00000000, 0x7D26122E },  // cave: enemy kill counts
  { 0x037FC, 0x00000000, 0x39290001 },  // cave: enemy kill counts
  { 0x03800, 0x00000000, 0x7D26132E },  // cave: enemy kill counts
  { 0x03804, 0x00000000, 0x2C030004 },  // cave: enemy kill counts
  { 0x03808, 0x00000000, 0x4080001C },  // cave: enemy kill counts
  { 0x0380C, 0x00000000, 0x388DC0A0 },  // cave: enemy kill counts
  { 0x03810, 0x00000000, 0x7CE4132E },  // cave: enemy kill counts
  { 0x03814, 0x00000000, 0x38ADC0D0 },  // cave: enemy kill counts
  { 0x03818, 0x00000000, 0x7D05132E },  // cave: enemy kill counts
  { 0x0381C, 0x00000000, 0x38CDC0D8 },  // cave: enemy kill counts
  { 0x03820, 0x00000000, 0x7D26132E },  // cave: enemy kill counts
  { 0x03824, 0x00000000, 0x4801F24C },  // cave: enemy kill counts
  { 0x13388, 0x388DC0A0, 0x4BFF04A0 },  // results screen: read kills from 0x160150
  { 0x03828, 0x00000000, 0x5463083C },  // cave: results screen kills
  { 0x0382C, 0x00000000, 0x3C800016 },  // cave: results screen kills
  { 0x03830, 0x00000000, 0x60840150 },  // cave: results screen kills
  { 0x03834, 0x00000000, 0x7C641A2E },  // cave: results screen kills
  { 0x03838, 0x00000000, 0x4800FB5C },  // cave: results screen kills
  { 0x1BD14, 0x388DC0D8, 0x4BFE7B2C },  // HUD: read kills from 0x160150/0x160170
  { 0x03840, 0x00000000, 0x3C800016 },  // cave: lis r4, 0x16
  { 0x03844, 0x00000000, 0x60840170 },  // cave: ori r4, r4, 0x170 (Area KOs 0x160170)
  { 0x03848, 0x00000000, 0x40820008 },  // cave: bne +8 (if r3 != 2 [Arcade], keep 0x160170)
  { 0x0384C, 0x00000000, 0x3884FFE0 },  // cave: addi r4, r4, -0x20 (if r3 == 2 [Versus], 0x160150 Total KOs)
  { 0x03850, 0x00000000, 0x60000000 },  // cave: nop
  { 0x03854, 0x00000000, 0x60000000 },  // cave: nop
  { 0x03858, 0x00000000, 0x60000000 },  // cave: nop
  { 0x0385C, 0x00000000, 0x480184C4 },  // cave: b 0x1BD20
  { 0x1C330, 0x388DC0A0, 0x4BFE75E0 },  // ally HUD: read kills from 0x160150/60/70
  { 0x03910, 0x00000000, 0x3C800016 },  // cave: ally HUD kill display
  { 0x03914, 0x00000000, 0x60840150 },  // cave: ally HUD kill display
  { 0x03918, 0x00000000, 0x3CA00016 },  // cave: ally HUD kill display
  { 0x0391C, 0x00000000, 0x60A50160 },  // cave: ally HUD kill display
  { 0x03920, 0x00000000, 0x3CC00016 },  // cave: ally HUD kill display
  { 0x03924, 0x00000000, 0x60C60170 },  // cave: ally HUD kill display
  { 0x03928, 0x00000000, 0x48018A14 },  // cave: ally HUD kill display
  // 11/16 (user: "SHIFT+START shows only 4 of 8 allies"): the ally stats pages (0x1C230) step a timer at +0xA4 from 0x140
  // by 0x80 frames per player (index = (t-0x141)/0x80) and stopped at 0x340 = 4 players. 0x540 = 8 players (absent ones
  // are skipped by the in-game-mask test at 0x1C2CC as before).
  { 0x1C26C, 0x2D030340, 0x2D030540 },  // ally stats: cmpwi cr2, r3, 0x540 (end after 8 players)
  { 0x1C5E0, 0x2C830340, 0x2C830540 },  // ally stats (skip): cmpwi cr1, r3, 0x540
  //    The KO / PK page still read the old 4-byte arrays (+0xA8/+0xAC) that the 8-player code no longer writes (KO/PK
  //    moved to 0x1601E0 / 0x1601E8, 8 bytes each). Read the moved arrays: r4 = 0x160000 + player.
  { 0x1C4F0, 0x386C00A8, 0x3C800016 },  // ally stats KO/PK: lis r4, 0x16
  { 0x1C4F4, 0x388C00AC, 0x7C848A14 },  // ally stats KO/PK: add r4, r4, r17
  { 0x1C4F8, 0x7C6388AE, 0x886401E0 },  // ally stats KO/PK: lbz r3, 0x1E0(r4) (KO[player])
  { 0x1C4FC, 0x7C8488AE, 0x888401E8 },  // ally stats KO/PK: lbz r4, 0x1E8(r4) (PK[player])
  { 0x59AD8, 0x908DC0DC, 0x4BFA9D88 },  // stage init: clear 8-player kill tables
  { 0x03860, 0x00000000, 0x908DC0DC },  // cave: stage clear kill tables
  { 0x03864, 0x00000000, 0x3CA00016 },  // cave: stage clear kill tables
  { 0x03868, 0x00000000, 0x60A50150 },  // cave: stage clear kill tables
  { 0x0386C, 0x00000000, 0x90850000 },  // cave: stage clear kill tables
  { 0x03870, 0x00000000, 0x90850004 },  // cave: stage clear kill tables
  { 0x03874, 0x00000000, 0x90850008 },  // cave: stage clear kill tables
  { 0x03878, 0x00000000, 0x9085000C },  // cave: stage clear kill tables
  { 0x0387C, 0x00000000, 0x90850010 },  // cave: stage clear kill tables
  { 0x03880, 0x00000000, 0x90850014 },  // cave: stage clear kill tables
  { 0x03884, 0x00000000, 0x90850018 },  // cave: stage clear kill tables
  { 0x03888, 0x00000000, 0x9085001C },  // cave: stage clear kill tables
  { 0x0388C, 0x00000000, 0x90850020 },  // cave: stage clear kill tables
  { 0x03890, 0x00000000, 0x90850024 },  // cave: stage clear kill tables
  { 0x03894, 0x00000000, 0x90850028 },  // cave: stage clear kill tables
  { 0x03898, 0x00000000, 0x9085002C },  // cave: stage clear kill tables
  { 0x0389C, 0x00000000, 0x90850030 },  // cave: stage clear kill tables
  { 0x038A0, 0x00000000, 0x90850034 },  // cave: stage clear kill tables
  { 0x038A4, 0x00000000, 0x4805623C },  // cave: stage clear kill tables
  // 11/08 removed: reset the owner's special stock 0 -> 3 on one node only -> special fires there only -> link desync
  // { 0x42F30, 0x886F3F25, 0x4BFC0978 },  // player init: ensure special stock 3
  { 0x038A8, 0x00000000, 0x886F3F25 },  // cave: player init special stock
  { 0x038AC, 0x00000000, 0x2C030000 },  // cave: player init special stock
  { 0x038B0, 0x00000000, 0x40820010 },  // cave: player init special stock
  { 0x038B4, 0x00000000, 0x38600003 },  // cave: player init special stock
  { 0x038B8, 0x00000000, 0x986F3F25 },  // cave: player init special stock
  { 0x038BC, 0x00000000, 0x986F3F26 },  // cave: player init special stock
  { 0x038C0, 0x00000000, 0x986E3F25 },  // cave: player init special stock: stb r3, 0x3f25(r14)
  { 0x038C4, 0x00000000, 0x4803F674 },  // cave: b 0x42F38 (0x038C4 + 0x3F674 = 0x42F38)
  { 0x038C8, 0x00000000, 0x00000000 },  // cave: nop
  // 11/08 removed: same node-local stock reset (0 -> 3) as 0x42F30
  // { 0x5A298, 0x989C3F24, 0x4BFA9638 },  // client restore: ensure special stock 3
  { 0x038D0, 0x00000000, 0x989C3F24 },  // cave: client restore special stock
  { 0x038D4, 0x00000000, 0x88BC3F25 },  // cave: client restore special stock
  { 0x038D8, 0x00000000, 0x2C050000 },  // cave: client restore special stock
  { 0x038DC, 0x00000000, 0x40820010 },  // cave: client restore special stock
  { 0x038E0, 0x00000000, 0x38A00003 },  // cave: client restore special stock
  { 0x038E4, 0x00000000, 0x98BC3F25 },  // cave: client restore special stock
  { 0x038E8, 0x00000000, 0x98BC3F26 },  // cave: client restore special stock
  { 0x038EC, 0x00000000, 0x480569B0 },  // cave: client restore special stock
  // 11/08 removed: respawn array A has 8 entries (0x106E40), slot&3 made players 5-8 continue at players 1-4's position
  // { 0x5B324, 0x1CC40010, 0x548626B6 },  // spawn pos: slot&3 * 0x10
  { 0x5B394, 0x5489083C, 0x4BFA855C },  // spawn: protect SDA overflow for slot >= 4
  { 0x038F0, 0x00000000, 0x5489083C },  // cave: spawn SDA protect
  { 0x038F4, 0x00000000, 0x2C040004 },  // cave: spawn SDA protect
  { 0x038F8, 0x00000000, 0x40800014 },  // cave: spawn SDA protect
  { 0x038FC, 0x00000000, 0x7D454B2E },  // cave: spawn SDA protect
  { 0x03900, 0x00000000, 0x7D464B2E },  // cave: spawn SDA protect
  { 0x03904, 0x00000000, 0x7D474B2E },  // cave: spawn SDA protect
  { 0x03908, 0x00000000, 0x7D4849AE },  // cave: spawn SDA protect
  { 0x0390C, 0x00000000, 0x48057A9C },  // cave: spawn SDA protect
  // HUD HP gauge fixes (05-08 left-alignment and Rob resolution)
  { 0x09788, 0x2D920004, 0x2D920008 },  // HUD energy gauge: check player slot up to 8 (cr3)
  { 0x098D4, 0x2D120004, 0x2D120008 },  // HUD energy gauge: left-alignment (r9=+2) for player slots 0-7
  { 0x098FC, 0x2C020004, 0x2C020008 },  // HUD energy gauge: Rob base player (0x1A) vs enemy (0x1B)
  { 0x0990C, 0x3842FFFC, 0x3842FFF8 },  // HUD energy gauge: enemy Rob offset (r2 - 8)
  { 0x09910, 0x3C00001B, 0x3C00001C },  // HUD energy gauge: enemy Rob base 0x1C0000
  { 0x09AC8, 0x2C020004, 0x2C020008 },  // HUD energy gauge: Rob base player (0x1A) vs enemy (0x1B)
  { 0x09AD8, 0x3842FFFC, 0x3842FFF8 },  // HUD energy gauge: enemy Rob offset (r2 - 8)
  { 0x09ADC, 0x3C00001B, 0x3C00001C },  // HUD energy gauge: enemy Rob base 0x1C0000
  { 0x09BAC, 0x2C020004, 0x2C020008 },  // HUD energy gauge: Rob base player (0x1A) vs enemy (0x1B)
  { 0x09BBC, 0x3842FFFC, 0x3842FFF8 },  // HUD energy gauge: enemy Rob offset (r2 - 8)
  { 0x09BC0, 0x3C00001B, 0x3C00001C },  // HUD energy gauge: enemy Rob base 0x1C0000
  // Score and kill count fixes for slots 4-7
  { 0x28608, 0x2C030004, 0x2C030008 },  // hit reaction: attacker slot up to 8
  { 0x29BA8, 0x2C030004, 0x2C030008 },  // score award from obj had: slot up to 8
  { 0x29BB4, 0x2C030004, 0x2C030008 },  // score award from obj take: slot up to 8
  { 0x29BC0, 0x2C020004, 0x2C020008 },  // score award: player 0-7 vs enemy Rob
  { 0x29BD0, 0x3842FFFC, 0x3842FFF8 },  // score award: enemy Rob offset (r2 - 8)
  { 0x29BD4, 0x3C00001B, 0x3C00001C },  // score award: enemy Rob base 0x1C0000
  { 0x29C38, 0x2C030004, 0x2C030008 },  // score award from killer: slot up to 8
  { 0x29C44, 0x2C020004, 0x2C020008 },  // score award from killer: player 0-7 vs enemy Rob
  { 0x29C54, 0x3842FFFC, 0x3842FFF8 },  // score award from killer: enemy Rob offset (r2 - 8)
  { 0x29C58, 0x3C00001B, 0x3C00001C },  // score award from killer: enemy Rob base 0x1C0000
  { 0x22704, 0x2C030004, 0x2C030008 },  // kill count array: killer slot up to 8
  // Player and enemy effect generation (aura, dust) for 8 players
  { 0x3EF40, 0x2C1F0003, 0x2C1F0007 },  // effect loop: player count 4 -> 8 (cmpwi r31, 7)
  { 0x3EF4C, 0x3DC0001B, 0x3DC0001C },  // effect loop: enemy Rob base 0x1B0000 -> 0x1C0000 (Enemy 4)
  { 0x3EF50, 0x3BC3000C, 0x3BC3001C },  // effect loop: fa_enemy offset 0xC -> 0x1C (Enemy 4)
  { 0x3EF94, 0x2C020004, 0x2C020008 },  // effect update: player 0-7 vs enemy (cmpwi r2, 8)
  { 0x3EFA4, 0x3842FFFC, 0x3842FFF8 },  // effect update: enemy Rob offset (r2 - 8)
  { 0x3EFA8, 0x3C00001B, 0x3C00001C },  // effect update: enemy Rob base 0x1C0000
  { 0x3F3AC, 0x2C030004, 0x2C030008 },  // effect generator: player slot < 8 (was < 4)
  { 0x3F3B0, 0x4D820020, 0x40800020 },  // effect generator: bgelr (return if slot >= 8)
  { 0x3F3C4, 0x2C030004, 0x2C030008 },  // effect generator: player slot < 8 (was < 4)
  { 0x3F3C8, 0x4D820020, 0x40800020 },  // effect generator: bgelr (return if slot >= 8)
  { 0x3FF20, 0x2C020004, 0x2C020008 },  // effect target: player 0-7 vs enemy (cmpwi r2, 8)
  { 0x3FF30, 0x3842FFFC, 0x3842FFF8 },  // effect target: enemy Rob offset (r2 - 8)
  { 0x3FF34, 0x3C00001B, 0x3C00001C },  // effect target: enemy Rob base 0x1C0000
  { 0x3F27C, 0x2C140004, 0x2C140008 },  // charge aura spawn: slot < 8 (was < 4)
  { 0x40108, 0x2C140004, 0x2C140008 },  // charge aura track: slot < 8 (was < 4)
  { 0x40114, 0x2C020004, 0x2C020008 },  // charge aura track: player 0-7 vs enemy (cmpwi r2, 8)
  { 0x40124, 0x3842FFFC, 0x3842FFF8 },  // charge aura track: enemy Rob offset (r2 - 8)
  { 0x40128, 0x3C00001B, 0x3C00001C },  // charge aura track: enemy Rob base 0x1C0000
  // --- Area Clear KO reset, packet stats, and score bonus for 8 players ---
  { 0x5CE78, 0x388DC0D8, 0x4BFA6B38 },  // area clear: hook kill stats reset to cave
  { 0x039B0, 0x00000000, 0x388DC0D8 },  // cave: area clear reset: addi r4, r13, -0x3f28
  { 0x039B4, 0x00000000, 0x38600000 },  // cave: area clear reset: li r3, 0
  { 0x039B8, 0x00000000, 0xB06DC048 },  // cave: area clear reset: sth r3, -0x3fb8(r13)
  { 0x039BC, 0x00000000, 0x986DC068 },  // cave: area clear reset: stb r3, -0x3f98(r13)
  { 0x039C0, 0x00000000, 0xB0640000 },  // cave: area clear reset: sth r3, 0(r4)
  { 0x039C4, 0x00000000, 0xB0640002 },  // cave: area clear reset: sth r3, 2(r4)
  { 0x039C8, 0x00000000, 0xB0640004 },  // cave: area clear reset: sth r3, 4(r4)
  { 0x039CC, 0x00000000, 0xB0640006 },  // cave: area clear reset: sth r3, 6(r4)
  { 0x039D0, 0x00000000, 0x986C02D4 },  // cave: area clear reset: stb r3, 0x2d4(r12)
  { 0x039D4, 0x00000000, 0x986DC069 },  // cave: area clear reset: stb r3, -0x3f97(r13)
  { 0x039D8, 0x00000000, 0x3CA00016 },  // cave: area clear reset: lis r5, 0x16
  { 0x039DC, 0x00000000, 0x60A50170 },  // cave: area clear reset: ori r5, r5, 0x170
  { 0x039E0, 0x00000000, 0x90650000 },  // cave: area clear reset: stw r3, 0(r5) (clear area kills 0-1)
  { 0x039E4, 0x00000000, 0x90650004 },  // cave: area clear reset: stw r3, 4(r5) (clear area kills 2-3)
  { 0x039E8, 0x00000000, 0x90650008 },  // cave: area clear reset: stw r3, 8(r5) (clear area kills 4-5)
  { 0x039EC, 0x00000000, 0x9065000C },  // cave: area clear reset: stw r3, 12(r5) (clear area kills 6-7)
  { 0x039F0, 0x00000000, 0x480594C0 },  // cave: area clear reset: b 0x5CEB0

  { 0x5CEB0, 0x388DC0A8, 0x4BFA6B44 },  // area/stage clear: hook special kill reset to cave
  { 0x039F4, 0x00000000, 0x388DC0A8 },  // cave: special kill reset: addi r4, r13, -0x3f58
  { 0x039F8, 0x00000000, 0x98640000 },  // cave: special kill reset: stb r3, 0(r4)
  { 0x039FC, 0x00000000, 0x98640001 },  // cave: special kill reset: stb r3, 1(r4)
  { 0x03A00, 0x00000000, 0x98640002 },  // cave: special kill reset: stb r3, 2(r4)
  { 0x03A04, 0x00000000, 0x98640003 },  // cave: special kill reset: stb r3, 3(r4)
  { 0x03A08, 0x00000000, 0x3CA00016 },  // cave: special kill reset: lis r5, 0x16
  { 0x03A0C, 0x00000000, 0x60A50180 },  // cave: special kill reset: ori r5, r5, 0x180
  { 0x03A10, 0x00000000, 0x90650000 },  // cave: special kill reset: stw r3, 0(r5) (clear special kills 0-3)
  { 0x03A14, 0x00000000, 0x90650004 },  // cave: special kill reset: stw r3, 4(r5) (clear special kills 4-7)
  { 0x03A18, 0x00000000, 0x4E800020 },  // cave: special kill reset: blr

  { 0x5CDB0, 0x388DC0D8, 0x4BFA6C6C },  // area transition packet: hook stats to cave
  { 0x03A1C, 0x00000000, 0x3C800016 },  // cave: area packet: lis r4, 0x16
  { 0x03A20, 0x00000000, 0x60840170 },  // cave: area packet: ori r4, r4, 0x170
  { 0x03A24, 0x00000000, 0x88660004 },  // cave: area packet: lbz r3, 4(r6)
  { 0x03A28, 0x00000000, 0x5463083C },  // cave: area packet: slwi r3, r3, 1
  { 0x03A2C, 0x00000000, 0x7CA3222E },  // cave: area packet: lhzx r5, r3, r4 (area kills)
  { 0x03A30, 0x00000000, 0x98A70033 },  // cave: area packet: stb r5, 0x33(r7)
  { 0x03A34, 0x00000000, 0x88660072 },  // cave: area packet: lbz r3, 0x72(r6)
  { 0x03A38, 0x00000000, 0x38840010 },  // cave: area packet: addi r4, r4, 0x10 (0x160180)
  { 0x03A3C, 0x00000000, 0x7CA418AE },  // cave: area packet: lbzx r5, r4, r3 (special kills)
  { 0x03A40, 0x00000000, 0x98A7003A },  // cave: area packet: stb r5, 0x3a(r7)
  { 0x03A44, 0x00000000, 0x48059390 },  // cave: area packet: b 0x5CDD4

  { 0x2A270, 0x5505083C, 0x4BFD97D8 },  // area clear bonus: hook KO score to cave
  { 0x03A48, 0x00000000, 0x5505083C },  // cave: area score bonus: slwi r5, r8, 1
  { 0x03A4C, 0x00000000, 0x3D400016 },  // cave: area score bonus: lis r10, 0x16
  { 0x03A50, 0x00000000, 0x614A0170 },  // cave: area score bonus: ori r10, r10, 0x170
  { 0x03A54, 0x00000000, 0x7D45522E },  // cave: area score bonus: lhzx r10, r5, r10
  { 0x03A58, 0x00000000, 0x48026824 },  // cave: area score bonus: b 0x2A27C
  // 11/20 (owner: bonus stage time-up scores for 05-08): the three bonus-stage results (0x1CF34 -> 0x2A418,
  // 0x1D020 -> 0x2A2B4, 0x1D08C -> 0x2A38C) and the other players' list (0x1D1A8) read each player's area KOs from the
  // 4-player table SDA -0x3F28 (0x1040D8), which players 5-8 never fill (cave 0x37B4: node < 4 only), so they read
  // 0x1040E0+ instead. Read the 8-player area KOs 0x160170 (r5 / r3 = slot * 2 already): addis + lhz 0x170.
  { 0x2A2F4, 0x394DC0D8, 0x3D450016 },  // bonus rank (0x2A2B4): addis r10, r5, 0x16
  { 0x2A2F8, 0x7D45522E, 0xA14A0170 },  // bonus rank (0x2A2B4): lhz r10, 0x170(r10)
  { 0x2A3D4, 0x394DC0D8, 0x3D450016 },  // bonus 50000 (0x2A38C): addis r10, r5, 0x16
  { 0x2A3D8, 0x7D45522E, 0xA14A0170 },  // bonus 50000 (0x2A38C): lhz r10, 0x170(r10)
  { 0x2A450, 0x394DC0D8, 0x3D450016 },  // KO count bonus 50000 (0x2A418): addis r10, r5, 0x16
  { 0x2A454, 0x7D45522E, 0xA14A0170 },  // KO count bonus 50000 (0x2A418): lhz r10, 0x170(r10)
  { 0x1D2B0, 0x388DC0D8, 0x3C830016 },  // results, other players' KOs (0x1D1A8): addis r4, r3, 0x16
  { 0x1D2B4, 0x7D23222E, 0xA1240170 },  // results, other players' KOs (0x1D1A8): lhz r9, 0x170(r4)
  // 11/20 (owner): the same for our own KO count shown at 0x1C790 (index = my Rob +4, stb 0x33(r12)); 0x1BD14 is another place.
  { 0x1C828, 0x388DC0D8, 0x5463083C },  // own KOs (0x1C790): slwi r3, r3, 1
  { 0x1C82C, 0x5463083C, 0x3C830016 },  // own KOs (0x1C790): addis r4, r3, 0x16
  { 0x1C830, 0x7C63222E, 0xA0640170 },  // own KOs (0x1C790): lhz r3, 0x170(r4)

  // --- HUD ally HP gauge: display 7 allies in 2 columns (4 in right column, 3 in left column) ---
  { 0x1AA84, 0x3874FFFE, 0x3B20002B },  // ally HUD: initial X = 0x2B (right column)
  { 0x1AA88, 0x1C630002, 0x3B000027 },  // ally HUD: initial Y = 0x27 (row 39)
  { 0x1AA8C, 0x7F03C050, 0x60000000 },  // ally HUD: nop out Y base subtraction
  { 0x1AA98, 0x2C1F0004, 0x2C1F0008 },  // ally HUD: loop limit 4 -> 8 players
  { 0x1AAC4, 0x38B7001C, 0x38B70018 },  // ally HUD pinch bit shift: r5 = 24 + r23 (fits 8 players in 0x59 byte, prevents voice loop)
  // 11/12 HP bar "recovering" flag for 8 players: was bit 12+slot of 0x104080 (4 players only; slots 4-7 set bits 16-19, never cleared -> ally bars flashed forever and polluted flags 0x1000-0x8000). Now bit slot of 0x3C5C.
  { 0x543C4, 0x808DC080, 0x80803C5C },  // 11/12 HP item: recovering flag -> 8-player word 0x3C5C: lwz r4,0x3C5C(0)
  { 0x543CC, 0x38A7000C, 0x38A70000 },  // 11/12 HP item: bit = slot (was 12+slot)
  { 0x543DC, 0x908DC080, 0x90803C5C },  // 11/12 HP item: stw r4,0x3C5C(0)
  { 0x1A828, 0x80EDC080, 0x80E03C5C },  // 11/12 HP bar fill 1A828: lwz r7,0x3C5C(0)
  { 0x1A830, 0x2C090004, 0x2C090008 },  // 11/12 HP bar fill 1A828: all 8 players (was slots 0-3)
  { 0x1A838, 0x3909000C, 0x39090000 },  // 11/12 HP bar fill 1A828: bit = slot
  { 0x1A8B0, 0x90EDC080, 0x90E03C5C },  // 11/12 HP bar fill 1A828: clear done flag: stw r7,0x3C5C(0)
  { 0x1B140, 0x80EDC080, 0x80E03C5C },  // 11/12 HP bar fill 1B140: lwz r7,0x3C5C(0)
  { 0x1B148, 0x2C090004, 0x2C090008 },  // 11/12 HP bar fill 1B140: all 8 players (was slots 0-3)
  { 0x1B150, 0x3909000C, 0x39090000 },  // 11/12 HP bar fill 1B140: bit = slot
  { 0x1B1C8, 0x90EDC080, 0x90E03C5C },  // 11/12 HP bar fill 1B140: clear done flag: stw r7,0x3C5C(0)
  { 0x1B214, 0x80EDC080, 0x80E03C5C },  // 11/12 HP bar fill 1B214: lwz r7,0x3C5C(0)
  { 0x1B21C, 0x2C090004, 0x2C090008 },  // 11/12 HP bar fill 1B214: all 8 players (was slots 0-3)
  { 0x1B224, 0x3909000C, 0x39090000 },  // 11/12 HP bar fill 1B214: bit = slot
  { 0x1B29C, 0x90EDC080, 0x90E03C5C },  // 11/12 HP bar fill 1B214: clear done flag: stw r7,0x3C5C(0)
  { 0x097AC, 0x806DC080, 0x80603C5C },  // 11/12 HP bar draw 097AC: lwz r3,0x3C5C(0)
  { 0x097BC, 0x3852000C, 0x38520000 },  // 11/12 HP bar draw 097AC: bit = player (was 12+player)
  { 0x0996C, 0x80CDC080, 0x80C03C5C },  // 11/12 HP bar draw 0996C: lwz r6,0x3C5C(0)
  { 0x0997C, 0x3872000C, 0x38720000 },  // 11/12 HP bar draw 0996C: bit = player (was 12+player)
  { 0x09AFC, 0x808DC080, 0x80803C5C },  // 11/12 HP bar draw 09AFC: lwz r4,0x3C5C(0)
  { 0x09B0C, 0x3872000C, 0x38720000 },  // 11/12 HP bar draw 09AFC: bit = player (was 12+player)
  { 0x09CB8, 0x800DC080, 0x80003C5C },  // 11/12 HP bar draw 09CB8: lwz r0,0x3C5C(0)
  { 0x09CC8, 0x3872000C, 0x38720000 },  // 11/12 HP bar draw 09CB8: bit = player (was 12+player)
  { 0x13FF8, 0x900DC080, 0x4BFEFC68 },  // 11/12 clear HUD word 13FF8: also clear 0x3C5C (cave 3C60)
  { 0x03C60, 0x00000000, 0x900DC080 },  // 11/12 cave 3C60: stw r0,-0x3f80(r13)
  { 0x03C64, 0x00000000, 0x90003C5C },  // 11/12 cave 3C60: stw r0,0x3C5C(0)
  { 0x03C68, 0x00000000, 0x48010394 },  // 11/12 cave 3C60: b back
  { 0x17940, 0x900DC080, 0x4BFEC32C },  // 11/12 clear HUD word 17940: also clear 0x3C5C (cave 3C6C)
  { 0x03C6C, 0x00000000, 0x900DC080 },  // 11/12 cave 3C6C: stw r0,-0x3f80(r13)
  { 0x03C70, 0x00000000, 0x90003C5C },  // 11/12 cave 3C6C: stw r0,0x3C5C(0)
  { 0x03C74, 0x00000000, 0x48013CD0 },  // 11/12 cave 3C6C: b back
  { 0x180D4, 0x900DC080, 0x4BFEBBA4 },  // 11/12 clear HUD word 180D4: also clear 0x3C5C (cave 3C78)
  { 0x03C78, 0x00000000, 0x900DC080 },  // 11/12 cave 3C78: stw r0,-0x3f80(r13)
  { 0x03C7C, 0x00000000, 0x90003C5C },  // 11/12 cave 3C78: stw r0,0x3C5C(0)
  { 0x03C80, 0x00000000, 0x48014458 },  // 11/12 cave 3C78: b back
  // 11/13 stage-clear heal (gate broken): 0x5BE48 sets the 4 players' "healing" bits 8-11 of 0x104080 at once and 0x5BEDC clears them. The HP bars now read 0x3C5C (17): mirror the state there as bits 8-15 (8 players) on every store.
  { 0x5BEF0, 0x90ADC080, 0x4BFA7E50 },  // 11/13 stage-clear heal flags: also mirror into 0x3C5C (cave 3D40)
  { 0x03D40, 0x00000000, 0x90ADC080 },  // 11/13 cave 3D40: stw r5,-0x3f80(r13) (moved here)
  { 0x03D44, 0x00000000, 0x80C03C5C },  // 11/13 cave 3D40: lwz r6,0x3C5C(0)
  { 0x03D48, 0x00000000, 0x54C6040E },  // 11/13 cave 3D40: clear bits 8-15 (stage-clear heal, 8 players)
  { 0x03D4C, 0x00000000, 0x54A70216 },  // 11/13 cave 3D40: r7 = the 4-player heal bits 8-11 of 0x104080
  { 0x03D50, 0x00000000, 0x2C070000 },  // 11/13 cave 3D40: cmpwi r7,0
  { 0x03D54, 0x00000000, 0x41820008 },  // 11/13 cave 3D40: beq +8
  { 0x03D58, 0x00000000, 0x64C600FF },  // 11/13 cave 3D40: oris r6,r6,0xFF: all 8 players healing
  { 0x03D5C, 0x00000000, 0x90C03C5C },  // 11/13 cave 3D40: stw r6,0x3C5C(0)
  { 0x03D60, 0x00000000, 0x4E800020 },  // 11/13 cave 3D40: blr (as 0x5BEF4)
  { 0x1AD14, 0x3B180002, 0x4BFE8D4C },  // ally HUD: hook Y step / column switch to cave 0x03A60
  // Cave 0x03A60: 2-column switcher
  { 0x03A60, 0x00000000, 0x3B180002 },  // cave: addi r24, r24, 2 (Y += 2)
  { 0x03A64, 0x00000000, 0x2C18002F },  // cave: cmpwi r24, 0x2F (right column 4 allies done?)
  { 0x03A68, 0x00000000, 0x4082000C },  // cave: bne +12 (continue)
  { 0x03A6C, 0x00000000, 0x3B200018 },  // cave: li r25, 0x18 (switch to left column X=24)
  { 0x03A70, 0x00000000, 0x3B000029 },  // cave: li r24, 0x29 (reset Y to row 41 for left column)
  { 0x03A74, 0x00000000, 0x3AF70001 },  // cave: addi r23, r23, 1 (slot++)
  { 0x03A78, 0x00000000, 0x39CE4000 },  // cave: addi r14, r14, 0x4000 (actor += 0x4000)
  { 0x03A7C, 0x00000000, 0x4801701C },  // cave: b 0x01AA98 (back to loop test)
  // Dual-column clear hook (0x01AD80 -> cave 0x03A80)
  { 0x1AD80, 0x396014D6, 0x4BFE8D00 },  // ally HUD: hook clear to cave 0x03A80
  { 0x03A80, 0x00000000, 0x396013D6 },  // cave: clear right column: li r11, 0x13D6 (Y=39, X=43)
  { 0x03A84, 0x00000000, 0x38800012 },  // cave: li r4, 0x12 (width 18)
  { 0x03A88, 0x00000000, 0x38A0000A },  // cave: li r5, 10 (height 10)
  { 0x03A8C, 0x00000000, 0x38C00020 },  // cave: li r6, 0x20 (space)
  { 0x03A90, 0x00000000, 0x3C600018 },  // cave: lis r3, 0x18
  { 0x03A94, 0x00000000, 0x3863730C },  // cave: addi r3, r3, 0x730c
  { 0x03A98, 0x00000000, 0x4800439D },  // cave: bl 0x7E34 (clear right column)
  { 0x03A9C, 0x00000000, 0x396014B0 },  // cave: clear left column: li r11, 0x14B0 (Y=41, X=24)
  { 0x03AA0, 0x00000000, 0x38800012 },  // cave: li r4, 0x12 (width 18)
  { 0x03AA4, 0x00000000, 0x38A00008 },  // cave: li r5, 8 (height 8)
  { 0x03AA8, 0x00000000, 0x38C00020 },  // cave: li r6, 0x20 (space)
  { 0x03AAC, 0x00000000, 0x3C600018 },  // cave: lis r3, 0x18
  { 0x03AB0, 0x00000000, 0x3863730C },  // cave: addi r3, r3, 0x730c
  { 0x03AB4, 0x00000000, 0x48004381 },  // cave: bl 0x7E34 (clear left column)
  { 0x03AB8, 0x00000000, 0x480172E4 },  // cave: b 0x01AD9C (return to original flow)

  // --- Gate route voter & candidate array: clear 8 candidate slots and set route table to 0x160C80 ---
  { 0x03AC0, 0x00000000, 0x906C002C },  // cave: route voter: clear candidate 4: stw r3, 0x2c(r12)
  { 0x03AC4, 0x00000000, 0x906C0030 },  // cave: route voter: clear candidate 5: stw r3, 0x30(r12)
  { 0x03AC8, 0x00000000, 0x906C0034 },  // cave: route voter: clear candidate 6: stw r3, 0x34(r12)
  { 0x03ACC, 0x00000000, 0x906C0038 },  // cave: route voter: clear candidate 7: stw r3, 0x38(r12)
  { 0x03AD0, 0x00000000, 0x3E800016 },  // cave: route voter: lis r20, 0x16
  { 0x03AD4, 0x00000000, 0x62940C80 },  // cave: route voter: ori r20, r20, 0x0c80 (r20 = 0x160c80)
  { 0x03AD8, 0x00000000, 0x480704E4 },  // cave: route voter: b 0x73FBC
  { 0x73FF0, 0x2C130004, 0x2C130008 },  // route voter: compare candidates loop limit 4 -> 8
  { 0x74028, 0x2C130004, 0x2C130008 },  // route voter: insert candidate loop limit 4 -> 8

  // --- Mode 5 Submode 5: Session master resolution for countdown timer & title gate opening ---
  { 0x04AE90, 0x3E600014, 0x4BFB8C50 },  // b 0x03AE0 (cave: resolve session master buffer instead of *(0x140004))
  { 0x04AE94, 0x82730004, 0x60000000 },  // nop
  { 0x03AE0, 0x00000000, 0xA272003C },  // cave: lhz r19, 0x3c(r18) (sequence number)
  { 0x03AE4, 0x00000000, 0x5673C23E },  // cave: srwi r19, r19, 8 (master node id)
  { 0x03AE8, 0x00000000, 0x2C130001 },  // cave: cmpwi r19, 1
  { 0x03AEC, 0x00000000, 0x41800030 },  // cave: blt local_master (0x03B1C)
  { 0x03AF0, 0x00000000, 0x2C130008 },  // cave: cmpwi r19, 8
  { 0x03AF4, 0x00000000, 0x41810028 },  // cave: bgt local_master (0x03B1C)
  { 0x03AF8, 0x00000000, 0x3C400010 },  // cave: lis r2, 0x10
  { 0x03AFC, 0x00000000, 0x8842065B },  // cave: lbz r2, 0x65b(r2) (local node id)
  { 0x03B00, 0x00000000, 0x7C131000 },  // cave: cmpw r19, r2
  { 0x03B04, 0x00000000, 0x41820018 },  // cave: beq local_master (0x03B1C)
  { 0x03B08, 0x00000000, 0x5673103A },  // cave: slwi r19, r19, 2
  { 0x03B0C, 0x00000000, 0x3E730015 },  // cave: addis r19, r19, 0x15
  { 0x03B10, 0x00000000, 0x3A730060 },  // cave: addi r19, r19, 0x60
  { 0x03B14, 0x00000000, 0x82730000 },  // cave: lwz r19, 0(r19) (node_ptrs[master])
  { 0x03B18, 0x00000000, 0x48000008 },  // cave: b 0x03B20
  { 0x03B1C, 0x00000000, 0x7E539378 },  // cave: mr r19, r18 (local buffer)
  { 0x03B20, 0x00000000, 0x48047378 },  // cave: b 0x04AE98 (resume)
  { 0x04A624, 0x2C040004, 0x2C040008 },  // Mode 3 Submode 21: Rob struct init limit 4 -> 8
  // --- Relocate node_ptrs table from 0x140014 to 0x140060 (prevents smashing 0x140028..0x140034) ---
  { 0x06D6E8, 0x3A520014, 0x3A520060 },  // node_ptrs base: addi r18, r18, 0x60 (was 0x14)
  { 0x12164, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x12964, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x15808, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x15DE0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x15E58, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x16134, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x16230, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x1A3CC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x23520, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x496E4, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x49760, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x499DC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4A830, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4A870, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4A9A0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4AA34, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4AA98, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4AD4C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4AEDC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4AF7C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4B000, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4B084, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4B11C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x4CB98, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x550EC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5B18C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5B278, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5B760, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5B900, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5C3CC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x5C5F4, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x609C0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6DC58, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6DDF8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6DE34, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6DEB4, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E168, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E1DC, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E224, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E308, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E420, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E490, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E598, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E640, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E73C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6E8D8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6EA08, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6EA54, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6EB8C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6EDA0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F480, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F4B8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F548, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F5E8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F708, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F754, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6F918, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6FA18, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6FB28, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6FB58, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x6FE38, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x73368, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x7341C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x734E0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x7351C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x735B8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x7370C, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x74220, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x743C8, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x74970, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x74D14, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x750C0, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x75258, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)
  { 0x755E4, 0x38420014, 0x38420060 },  // node_ptrs lookup: addi r2, r2, 0x60 (was 0x14)

  // --- Character Select: initialize 0x3F00 for all 8 player slots (0..7) ---
  { 0x75D38, 0x38800000, 0x38800000 },  // char select: li r4, 0
  { 0x75D3C, 0x9C834000, 0x9C834000 },  // char select: loop: stbu r4, 0x4000(r3)
  { 0x75D40, 0x38840001, 0x38840001 },  // char select: addi r4, r4, 1
  { 0x75D44, 0x9C834000, 0x2C040008 },  // char select: cmpwi r4, 8
  { 0x75D48, 0x38840001, 0x4180FFF4 },  // char select: blt loop (0x75D3C)
  { 0x75D4C, 0x9C834000, 0x60000000 },  // char select: nop
  { 0x75D50, 0x38840001, 0x60000000 },  // char select: nop
  { 0x75D54, 0x9C834000, 0x60000000 },  // char select: nop

  // --- Player Target Selection & Grab Resolution for 8 Players (01..08) ---
  // 1. Player & Enemy teammate target search (0x55430..0x55518): consider all 8 players (slots 0..7) and prevent infinite loop when r4 == 0!
  // Note: 0x5543C / 0x554EC clears bit (31 - r5) in r4; before looping back via blt, we MUST test cmpwi r4, 0 so r4==0 exits cleanly!
  { 0x55450, 0x2C050003, 0x4BFAE734 },  // player teammate search: hook 0x55450 to cave 0x03B84
  { 0x03B84, 0x00000000, 0x2C040000 },  // cave: cmpwi r4, 0 (any active player bits left in r4?)
  { 0x03B88, 0x00000000, 0x41820010 },  // cave: beq +16 (0x03B98 -> b 0x55458 fallback r5=0, r6=0)
  { 0x03B8C, 0x00000000, 0x2C050007 },  // cave: cmpwi r5, 7 (slots 0..7)
  { 0x03B90, 0x00000000, 0x4180000C },  // cave: blt +12 (0x03B9C -> b 0x55430 next candidate)
  { 0x03B94, 0x00000000, 0x480518C4 },  // cave: b 0x55458 (fallback)
  { 0x03B98, 0x00000000, 0x480518C0 },  // cave: b 0x55458 (fallback)
  { 0x03B9C, 0x00000000, 0x48051894 },  // cave: b 0x55430 (loop next candidate)
  { 0x55500, 0x2C05000F, 0x4BFAE6A0 },  // enemy teammate search: hook 0x55500 to cave 0x03BA0
  { 0x55508, 0x38A00000, 0x38A00004 },  // enemy teammate search: fallback slot 4 (0x1C0000), never 0 (Player 05 at 0x1B0000)
  { 0x03BA0, 0x00000000, 0x2C040000 },  // cave: cmpwi r4, 0 (any active enemy bits left in r4?)
  { 0x03BA4, 0x00000000, 0x41820010 },  // cave: beq +16 (0x03BB4 -> b 0x55508 fallback r5=4, r6=0)
  { 0x03BA8, 0x00000000, 0x2C05000F },  // cave: cmpwi r5, 15 (slots 4..15)
  { 0x03BAC, 0x00000000, 0x4180000C },  // cave: blt +12 (0x03BB8 -> b 0x554E0 next candidate)
  { 0x03BB0, 0x00000000, 0x48051958 },  // cave: b 0x55508 (fallback)
  { 0x03BB4, 0x00000000, 0x48051954 },  // cave: b 0x55508 (fallback)
  { 0x03BB8, 0x00000000, 0x48051928 },  // cave: b 0x554E0 (loop next candidate)

  // 2. Player enemy target selection (0x539E4): only search/fallback to real enemies 4..15 (never Player 05 at 0x1B0000)
  { 0x539F8, 0x3BA00010, 0x3BA0000C },  // Player_PickEnemyTarget: li r29, 12 (search 12 enemy slots 4..15)
  { 0x53B7C, 0x38E00000, 0x38E00004 },  // Player_PickEnemyTarget: li r7, 4 (start search at enemy slot 4 = 0x1C0000)
  { 0x53B80, 0x39000000, 0x39000004 },  // Player_PickEnemyTarget: li r8, 4 (fallback enemy slot 4 = 0x1C0000, never 0 = Player 05)
  { 0x539F4, 0x8A8E325F, 0x4BFB0130 },  // Player_PickEnemyTarget: hook initial r20 load to cave 0x03B24 (clamp r20 >= 4)
  { 0x03B24, 0x00000000, 0x8A8E325F },  // cave: lbz r20, 0x325f(r14)
  { 0x03B28, 0x00000000, 0x2C140004 },  // cave: cmpwi r20, 4
  { 0x03B2C, 0x00000000, 0x40800008 },  // cave: bge +8 (0x03B34)
  { 0x03B30, 0x00000000, 0x3A800004 },  // cave: li r20, 4 (default to enemy slot 4 = 0x1C0000)
  { 0x03B34, 0x00000000, 0x4804FEC4 },  // cave: b 0x539F8
  { 0x53CEC, 0x4BFFFFA8, 0x4BFAFE4C },  // Player_PickEnemyTarget: hook grab partner branch (0x53CEC) to cave 0x03B38
  { 0x03B38, 0x00000000, 0x800F0044 },  // cave: lwz r0, 0x44(r15) (check if grab partner is enemy or player)
  { 0x03B3C, 0x00000000, 0x5400F7FE },  // cave: rlwinm. r0, r0, 30, 31, 31 (test enemy bit 30)
  { 0x03B40, 0x00000000, 0x41820008 },  // cave: beq +8 (0x03B48: partner is player -> keep r15 from 0x110(r14)!)
  { 0x03B44, 0x00000000, 0x48050150 },  // cave: b 0x53C94 (partner is enemy -> compute 0x1B0000 + r20*0x4000)
  { 0x03B48, 0x00000000, 0x48050158 },  // cave: b 0x53CA0 (preserve r15 and r14->0x325f)

  // 3. Target flag calculation (0x54BDC): exclude self-targeting (r14 == r15) so distance=0 never sets grab flags on self
  { 0x54BDC, 0x3A400000, 0x4BFAEF70 },  // hook 0x54BDC entry to cave 0x03B4C
  { 0x03B4C, 0x00000000, 0x3A400000 },  // cave: li r18, 0
  { 0x03B50, 0x00000000, 0x7C0E7800 },  // cave: cmpw r14, r15
  { 0x03B54, 0x00000000, 0x41820008 },  // cave: beq +8 (0x03B5C: self -> clear flags and return)
  { 0x03B58, 0x00000000, 0x48051088 },  // cave: b 0x54BE0 (normal target evaluation)
  { 0x03B5C, 0x00000000, 0x57E4103A },  // cave: rlwinm r4, r31, 2, 0, 29 (r31 * 4)
  { 0x03B60, 0x00000000, 0x7E56212E },  // cave: stwx r18, r22, r4 (clear +0x3444[self])
  { 0x03B64, 0x00000000, 0x4E800020 },  // cave: blr

  // 4. Grab resolution (0x1E2EC): null-check event 5 lookup (bl 0x21520) and prevent self-grab (r14 == r15)
  { 0x1E2EC, 0xA0A30002, 0x4BFE587C },  // hook 0x1E2EC to cave 0x03B68
  { 0x03B68, 0x00000000, 0x2C030000 },  // cave: cmpwi r3, 0 (did 0x21520 find event 5?)
  { 0x03B6C, 0x00000000, 0x41820014 },  // cave: beq +20 (0x03B80: abort grab cleanly if null)
  { 0x03B70, 0x00000000, 0x7C0E7800 },  // cave: cmpw r14, r15 (prevent self-grab)
  { 0x03B74, 0x00000000, 0x4182000C },  // cave: beq +12 (0x03B80: abort grab if r14 == r15)
  { 0x03B78, 0x00000000, 0xA0A30002 },  // cave: lhz r5, 2(r3) (original instruction)
  { 0x03B7C, 0x00000000, 0x4801A774 },  // cave: b 0x1E2F0 (continue grab)
  { 0x03B80, 0x00000000, 0x4801A7C8 },  // cave: b 0x1E348 (abort grab)

  // 5. Item pickup (11/11): the obj_item handler (0x3A050) looks for the nearest player within reach of a dropped
  //    item (food / recovery) in a loop over 4 Robs from 0x1A0000 - players 5-8 (0x1B0000..) could not take items.
  //    The rest of the handler already maps the taker's slot 4-7 to 0x1B0000.
  { 0x3A1B8, 0x39400004, 0x39400008 },  // item pickup: nearest-player loop over 8 players (li r10, 4 -> 8)

  // --- Boss HP scaling for 5-8 players
  // The game raises a boss's HP (list entry +0x15 >= 4 / generator entry +0x03 >= 4) by player count, but reads
  // the count capped at 4 (0x106180): 2p x1.25, 3p x1.375, 4p x1.5. The caves count the in-game mask (0x140128);
  // with 5-8 players HP += HP*n/8 (5p x1.625, 6p x1.75, 7p x1.875, 8p x2.0), otherwise the original code runs.
  { 0x03CA0, 0x00000000, 0x3CC00014 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): lis r6, 0x14
  { 0x03CA4, 0x00000000, 0x80E60128 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): lwz r7, 0x128(r6)
  { 0x03CA8, 0x00000000, 0x38C00000 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): li r6, 0
  { 0x03CAC, 0x00000000, 0x2C070000 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): cmpwi r7, 0
  { 0x03CB0, 0x00000000, 0x41820014 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): beq 0x3cc4
  { 0x03CB4, 0x00000000, 0x3907FFFF },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): addi r8, r7, -1
  { 0x03CB8, 0x00000000, 0x7CE74038 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): and r7, r7, r8
  { 0x03CBC, 0x00000000, 0x38C60001 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): addi r6, r6, 1
  { 0x03CC0, 0x00000000, 0x4BFFFFEC },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): b 0x3cac
  { 0x03CC4, 0x00000000, 0x2C060005 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): cmpwi r6, 5
  { 0x03CC8, 0x00000000, 0x4180001C },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): blt 0x3ce4
  { 0x03CCC, 0x00000000, 0xA0E40012 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): lhz r7, 0x12(r4)
  { 0x03CD0, 0x00000000, 0x7D0731D6 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): mullw r8, r7, r6
  { 0x03CD4, 0x00000000, 0x5508E8FE },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): srwi r8, r8, 3
  { 0x03CD8, 0x00000000, 0x7CE74214 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): add r7, r7, r8
  { 0x03CDC, 0x00000000, 0xB0E40012 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): sth r7, 0x12(r4)
  { 0x03CE0, 0x00000000, 0x480593B0 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): b 0x5d090
  { 0x03CE4, 0x00000000, 0x88CDE180 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): lbz r6, -0x1e80(r13)
  { 0x03CE8, 0x00000000, 0x48059364 },  // cave: boss HP x(1+n/8) for 5-8 players (list spawn): b 0x5d04c
  { 0x03CF0, 0x00000000, 0x3C600014 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): lis r3, 0x14
  { 0x03CF4, 0x00000000, 0x80830128 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): lwz r4, 0x128(r3)
  { 0x03CF8, 0x00000000, 0x38600000 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): li r3, 0
  { 0x03CFC, 0x00000000, 0x2C040000 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): cmpwi r4, 0
  { 0x03D00, 0x00000000, 0x41820014 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): beq 0x3d14
  { 0x03D04, 0x00000000, 0x38A4FFFF },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): addi r5, r4, -1
  { 0x03D08, 0x00000000, 0x7C842838 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): and r4, r4, r5
  { 0x03D0C, 0x00000000, 0x38630001 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): addi r3, r3, 1
  { 0x03D10, 0x00000000, 0x4BFFFFEC },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): b 0x3cfc
  { 0x03D14, 0x00000000, 0x2C030005 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): cmpwi r3, 5
  { 0x03D18, 0x00000000, 0x4180001C },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): blt 0x3d34
  { 0x03D1C, 0x00000000, 0xA0910012 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): lhz r4, 0x12(r17)
  { 0x03D20, 0x00000000, 0x7CA419D6 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): mullw r5, r4, r3
  { 0x03D24, 0x00000000, 0x54A5E8FE },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): srwi r5, r5, 3
  { 0x03D28, 0x00000000, 0x7C842A14 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): add r4, r4, r5
  { 0x03D2C, 0x00000000, 0xB0910012 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): sth r4, 0x12(r17)
  { 0x03D30, 0x00000000, 0x48057314 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): b 0x5b044
  { 0x03D34, 0x00000000, 0x886DE180 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): lbz r3, -0x1e80(r13)
  { 0x03D38, 0x00000000, 0x480572C8 },  // cave: boss HP x(1+n/8) for 5-8 players (generator spawn): b 0x5b000
  { 0x5D048, 0x88CDE180, 0x4BFA6C58 },  // boss HP scaling (list spawn): 5-8 players via cave 0x3CA0
  { 0x5AFFC, 0x886DE180, 0x4BFA8CF4 },  // boss HP scaling (generator spawn): 5-8 players via cave 0x3CF0

  // --- Event script player motions: player p also drives player p+4 (11/14) ---
  // Script ops 0x3A (0x5E32C: Rob +0x34 = value) and 0x44 (0x5E628: Rob +0x34 = (+0x54 table)[index]) address
  // players only as 0xFFFF-0xFFFC (players 1-4; 0xFFFB = all, already 8 via 0x51854). Area 53 (Michael) gives the
  // escape motions this way, so players 5-8 stayed behind and the last shot showed only 4 (also areas 45, 71).
  // Rewritten in place (no cave): Rob p = 0x1A0000 + p*0x4000, then the same write to Rob p + 0x10000 (= slot p+4),
  // each only if its +0 bit 0 is set (as the original check). The loop ends when bit 16 of r3 clears (0x1C....).
  { 0x5E390, 0x2C020004, 0x1C424000 },  // op 0x3A: mulli r2, r2, 0x4000
  { 0x5E394, 0x4080000C, 0x3C62001A },  // op 0x3A: addis r3, r2, 0x1a (Rob p)
  { 0x5E398, 0x3C00001A, 0x80830000 },  // op 0x3A: lwz r4, 0(r3)
  { 0x5E39C, 0x4800000C, 0x5480F801 },  // op 0x3A: rlwinm. r0, r4, 0x1f, 0, 0
  { 0x5E3A0, 0x3842FFFC, 0x41820008 },  // op 0x3A: beq 0x5e3a8
  { 0x5E3A4, 0x3C00001B, 0x92430034 },  // op 0x3A: stw r18, 0x34(r3)
  { 0x5E3A8, 0x1C424000, 0x3C630001 },  // op 0x3A: addis r3, r3, 1 (slot p+4)
  { 0x5E3AC, 0x7C420214, 0x74600001 },  // op 0x3A: andis. r0, r3, 1
  { 0x5E3B0, 0x38620000, 0x4082FFE8 },  // op 0x3A: bne 0x5e398
  { 0x5E3B4, 0x80830000, 0x48000010 },  // op 0x3A: b 0x5e3c4
  { 0x5E67C, 0x2C020004, 0x1C424000 },  // op 0x44: mulli r2, r2, 0x4000
  { 0x5E680, 0x4080000C, 0x3C62001A },  // op 0x44: addis r3, r2, 0x1a (Rob p)
  { 0x5E684, 0x3C00001A, 0x80830000 },  // op 0x44: lwz r4, 0(r3)
  { 0x5E688, 0x4800000C, 0x5480F801 },  // op 0x44: rlwinm. r0, r4, 0x1f, 0, 0
  { 0x5E68C, 0x3842FFFC, 0x41820010 },  // op 0x44: beq 0x5e69c
  { 0x5E690, 0x3C00001B, 0x80830054 },  // op 0x44: lwz r4, 0x54(r3)
  { 0x5E694, 0x1C424000, 0x7C84922E },  // op 0x44: lhzx r4, r4, r18
  { 0x5E698, 0x7C420214, 0x90830034 },  // op 0x44: stw r4, 0x34(r3)
  { 0x5E69C, 0x38620000, 0x3C630001 },  // op 0x44: addis r3, r3, 1 (slot p+4)
  { 0x5E6A0, 0x80830000, 0x74600001 },  // op 0x44: andis. r0, r3, 1
  { 0x5E6A4, 0x5480F801, 0x4082FFE0 },  // op 0x44: bne 0x5e684
  { 0x5E6A8, 0x41820010, 0x48000010 },  // op 0x44: b 0x5e6b8
  // 11/15 cloth (Linda's skirt flaps etc. - models the CPU builds into polygon RAM each frame, 0x55AC8): the loop 0x56ACC
  // over the 7 nearest objects (list 0x3004F4, work 0x300538 + index*0x3600) checks each owner's status word - index
  // 0-3 fa_player +0x10 + i*4, index 4+ fa_enemy +0x10 + (i-4)*4. Players 5-8 are indexes 4-7, whose fa_enemy words
  // stay 0 in the 8-player game -> no cloth for them (05-08 see-through skirts, 11/15). Indexes 4-7: the Rob's own
  // flags word (0x1A0000 + i*0x4000 +0, which the fa_player words copy); 8+: fa_enemy as before. Cave 0x3DDC-0x3DFF.
  { 0x56B60, 0x808DC118, 0x4BFAD27C },  // cloth owner > 3: b 0x3ddc
  { 0x03DDC, 0x00000000, 0x2C130007 },  // cave cloth: cmpwi r19, 7
  { 0x03DE0, 0x00000000, 0x41810014 },  // cave cloth: bgt 0x3df4 (an enemy)
  { 0x03DE4, 0x00000000, 0x3C80001A },  // cave cloth: lis r4, 0x1a
  { 0x03DE8, 0x00000000, 0x1C734000 },  // cave cloth: mulli r3, r19, 0x4000
  { 0x03DEC, 0x00000000, 0x7CA4182E },  // cave cloth: lwzx r5, r4, r3 (player 5-8's Rob flags)
  { 0x03DF0, 0x00000000, 0x48052D84 },  // cave cloth: b 0x56b74 (rlwinm. / beq as before)
  { 0x03DF4, 0x00000000, 0x808DC118 },  // cave cloth: lwz r4, -0x3ee8(r13) (as before)
  { 0x03DF8, 0x00000000, 0x3873FFFC },  // cave cloth: addi r3, r19, -4
  { 0x03DFC, 0x00000000, 0x48052D6C },  // cave cloth: b 0x56b68
  // 11/15 (user): the HP the players get back after a gate (0x22AE8, called from 0x5BF44's 8-player loop) is raised by the
  // player count through the table 0x8B674 (0x227E8: 1p x1, 2p x1.25, 3p x1.5, 4p x1.75, count capped at 4). The cave
  // 0x3F00 counts the in-game mask 0x140128: 5-8 players -> r4 = r16 + r16*(n-1)/4 (5p x2.0 ... 8p x2.75); otherwise the
  // original 0x227E8. The cap of 2/3 of max HP after it is kept.
  { 0x22AFC, 0x4BFFFCED, 0x4BFE1405 },  // gate heal: bl 0x227e8 -> bl 0x3f00
  { 0x03F00, 0x00000000, 0x3C800014 },  // cave heal: lis r4, 0x14
  { 0x03F04, 0x00000000, 0x80840128 },  // cave heal: lwz r4, 0x128(r4) (in-game mask)
  { 0x03F08, 0x00000000, 0x38400000 },  // cave heal: li r2, 0
  { 0x03F0C, 0x00000000, 0x2C040000 },  // cave heal: cmpwi r4, 0
  { 0x03F10, 0x00000000, 0x41820014 },  // cave heal: beq 0x3f24
  { 0x03F14, 0x00000000, 0x3804FFFF },  // cave heal: addi r0, r4, -1
  { 0x03F18, 0x00000000, 0x7C840038 },  // cave heal: and r4, r4, r0
  { 0x03F1C, 0x00000000, 0x38420001 },  // cave heal: addi r2, r2, 1
  { 0x03F20, 0x00000000, 0x4BFFFFEC },  // cave heal: b 0x3f0c
  { 0x03F24, 0x00000000, 0x2C020004 },  // cave heal: cmpwi r2, 4
  { 0x03F28, 0x00000000, 0x40810018 },  // cave heal: ble 0x3f40 (4 or fewer: the original table)
  { 0x03F2C, 0x00000000, 0x3842FFFF },  // cave heal: addi r2, r2, -1
  { 0x03F30, 0x00000000, 0x7C9011D6 },  // cave heal: mullw r4, r16, r2
  { 0x03F34, 0x00000000, 0x5484F0BE },  // cave heal: srwi r4, r4, 2
  { 0x03F38, 0x00000000, 0x7C848214 },  // cave heal: add r4, r4, r16
  { 0x03F3C, 0x00000000, 0x4E800020 },  // cave heal: blr
  { 0x03F40, 0x00000000, 0x4801E8A8 },  // cave heal: b 0x227e8
  // 11/15 (user): the damage a PLAYER takes (0x26100-: victim Rob +0 without the enemy bit 0x2) is raised by the player
  // count through the table 0x8B684 (0x22834: 1p x1, 2p x1.25, 3p x1.5, 4p x1.75, count capped at 4) into Rob +0x34E2.
  // The cave 0x3F44 (r3 kept on the stack) counts the in-game mask 0x140128: 5-8 players -> r4 = r16 + r16*(n-1)/4
  // (5p x2.0 ... 8p x2.75); otherwise the original 0x22834. Both callers (0x261A8, 0x26250).
  { 0x261A8, 0x4BFFC68D, 0x4BFDDD9D },  // player damage: bl 0x22834 -> bl 0x3f44
  { 0x26250, 0x4BFFC5E5, 0x4BFDDCF5 },  // player damage: bl 0x22834 -> bl 0x3f44
  { 0x03F44, 0x00000000, 0x3821FFF0 },  // cave dmg: addi r1, r1, -0x10
  { 0x03F48, 0x00000000, 0x90610000 },  // cave dmg: stw r3, 0(r1)
  { 0x03F4C, 0x00000000, 0x3C800014 },  // cave dmg: lis r4, 0x14
  { 0x03F50, 0x00000000, 0x80840128 },  // cave dmg: lwz r4, 0x128(r4) (in-game mask)
  { 0x03F54, 0x00000000, 0x38600000 },  // cave dmg: li r3, 0
  { 0x03F58, 0x00000000, 0x2C040000 },  // cave dmg: cmpwi r4, 0
  { 0x03F5C, 0x00000000, 0x41820014 },  // cave dmg: beq 0x3f70
  { 0x03F60, 0x00000000, 0x3804FFFF },  // cave dmg: addi r0, r4, -1
  { 0x03F64, 0x00000000, 0x7C840038 },  // cave dmg: and r4, r4, r0
  { 0x03F68, 0x00000000, 0x38630001 },  // cave dmg: addi r3, r3, 1
  { 0x03F6C, 0x00000000, 0x4BFFFFEC },  // cave dmg: b 0x3f58
  { 0x03F70, 0x00000000, 0x2C030004 },  // cave dmg: cmpwi r3, 4
  { 0x03F74, 0x00000000, 0x40810020 },  // cave dmg: ble 0x3f94 (4 or fewer: the original table)
  { 0x03F78, 0x00000000, 0x3863FFFF },  // cave dmg: addi r3, r3, -1
  { 0x03F7C, 0x00000000, 0x7C9019D6 },  // cave dmg: mullw r4, r16, r3
  { 0x03F80, 0x00000000, 0x5484F0BE },  // cave dmg: srwi r4, r4, 2
  { 0x03F84, 0x00000000, 0x7C848214 },  // cave dmg: add r4, r4, r16
  { 0x03F88, 0x00000000, 0x80610000 },  // cave dmg: lwz r3, 0(r1)
  { 0x03F8C, 0x00000000, 0x38210010 },  // cave dmg: addi r1, r1, 0x10
  { 0x03F90, 0x00000000, 0x4E800020 },  // cave dmg: blr
  { 0x03F94, 0x00000000, 0x80610000 },  // cave dmg: lwz r3, 0(r1)
  { 0x03F98, 0x00000000, 0x38210010 },  // cave dmg: addi r1, r1, 0x10
  { 0x03F9C, 0x00000000, 0x4801E898 },  // cave dmg: b 0x22834
  // 11/15 night: join screen (0x12904) lists the players already in the session (fa(SDA -0x3EBC)+0x52.., 3 bytes)
  // and picks their pictures from 0x8F4A4[count-1] (3 entries). With 4+ in play it read 0x1A21 (Spike -> picture
  // 0x4008, 99 parts -> "[CPM]: No idle cpic.") and wrote over +0x55... Keep at most 3 in the list.
  { 0x12984, 0x7CB589AE, 0x4BFF161C },  // join screen list: b 0x3fa0
  { 0x03FA0, 0x00000000, 0x2C110003 },  // cave join list: cmpwi r17, 3
  { 0x03FA4, 0x00000000, 0x4080000C },  // cave join list: bge 0x3fb0
  { 0x03FA8, 0x00000000, 0x7CB589AE },  // cave join list: stbx r5, r21, r17
  { 0x03FAC, 0x00000000, 0x3A310001 },  // cave join list: addi r17, r17, 1
  { 0x03FB0, 0x00000000, 0x4800E9DC },  // cave join list: b 0x1298c
  // 11/16 night (owner): GAME SELECT ("which game to join?", mode 5/3, 0x73E78 -> 0x73F6C; work *(SDA -0x3EC8)):
  // the selection +0x14 starts at 0 = the first game in the list (GAME A); the lever adds -1/+1 at 0x74098, and an
  // empty / closed (0x0F) entry falls through to -1 = "start from the beginning" (a new, separate game). Found by
  // comparing save states (GAME A / B / new: +0x14 = 0 / 1 / -1). The lever no longer moves it: with a session the
  // pick is always GAME A; with none (or A closed) the original check still gives "start from the beginning".
  // (The 8-player patch's "route voter" entries above are this same list, widened to 8 candidates.)
  { 0x74098, 0x7E101A14, 0x60000000 },  // game select: add r16, r16, r3 (lever) -> nop
  // 11/16 night (owner: after the 2nd mid-join enemy textures are garbled until the next area): the texture request
  // queue (8 nodes) still overflows when several players have joined, and 0x580B0 (enemy picture cache upload) drops
  // the request while the enemy cache table (fa_enemy +0x11C, 5 x 8 bytes: +0 picture id, +4/+6 users) already says
  // "loaded" (the 11/11 costume de-dup only made the first join fit). Now a dropped enemy upload is retried:
  //  - cave 0x3FB4 wraps the queue allocation (0x5870C) in 0x580B0: when no node was free (r26 = -1) it sets a
  //    "re-upload" mark in the entry's unused byte +7 (fa_enemy +0x123 + (slot-1)*8; slot = r4 = 1..5).
  //  - cave 0x30C8 runs first in the texture task every frame (0x582B4): for each marked entry it clears the mark and,
  //    if the entry still holds a picture, calls 0x580B0 again (dropped again -> marked again, next frame).
  // The table init (0x52100) only writes +0, nothing else uses +7. Player costumes (0x58154) are not retried here.
  { 0x580C8, 0x48000645, 0x4BFABEED },  // enemy upload: bl 0x5870c -> bl 0x3fb4 (mark when dropped)
  { 0x03FB4, 0x00000000, 0x7CE802A6 },  // cave tex mark: mflr r7
  { 0x03FB8, 0x00000000, 0x48054755 },  // cave tex mark: bl 0x5870c
  { 0x03FBC, 0x00000000, 0x7CE803A6 },  // cave tex mark: mtlr r7
  { 0x03FC0, 0x00000000, 0x2C1AFFFF },  // cave tex mark: cmpwi r26, -1
  { 0x03FC4, 0x00000000, 0x4C820020 },  // cave tex mark: bnelr (a node was free: as before)
  { 0x03FC8, 0x00000000, 0x810DC118 },  // cave tex mark: lwz r8, -0x3ee8(r13) (fa_enemy)
  { 0x03FCC, 0x00000000, 0x38C4FFFF },  // cave tex mark: addi r6, r4, -1
  { 0x03FD0, 0x00000000, 0x28060004 },  // cave tex mark: cmplwi r6, 4
  { 0x03FD4, 0x00000000, 0x4D810020 },  // cave tex mark: bgtlr (not slot 1-5)
  { 0x03FD8, 0x00000000, 0x54C6183A },  // cave tex mark: slwi r6, r6, 3
  { 0x03FDC, 0x00000000, 0x7D083214 },  // cave tex mark: add r8, r8, r6
  { 0x03FE0, 0x00000000, 0x38C00001 },  // cave tex mark: li r6, 1
  { 0x03FE4, 0x00000000, 0x98C80123 },  // cave tex mark: stb r6, 0x123(r8) (entry +7)
  { 0x03FE8, 0x00000000, 0x4E800020 },  // cave tex mark: blr
  { 0x582CC, 0x480004E1, 0x4BFAADFD },  // texture task: bl 0x587ac -> bl 0x30c8 (retry first)
  { 0x030C8, 0x00000000, 0x3821FFF0 },  // cave tex retry: addi r1, r1, -0x10
  { 0x030CC, 0x00000000, 0x7C0802A6 },  // cave tex retry: mflr r0
  { 0x030D0, 0x00000000, 0x90010000 },  // cave tex retry: stw r0, 0(r1)
  { 0x030D4, 0x00000000, 0x92010004 },  // cave tex retry: stw r16, 4(r1)
  { 0x030D8, 0x00000000, 0x3A000005 },  // cave tex retry: li r16, 5 (slot)
  { 0x030DC, 0x00000000, 0x5606183A },  // cave tex retry: slwi r6, r16, 3
  { 0x030E0, 0x00000000, 0x80EDC118 },  // cave tex retry: lwz r7, -0x3ee8(r13) (fa_enemy)
  { 0x030E4, 0x00000000, 0x7CC63A14 },  // cave tex retry: add r6, r6, r7
  { 0x030E8, 0x00000000, 0x88A6011B },  // cave tex retry: lbz r5, 0x11b(r6) (mark)
  { 0x030EC, 0x00000000, 0x2C050000 },  // cave tex retry: cmpwi r5, 0
  { 0x030F0, 0x00000000, 0x41820020 },  // cave tex retry: beq next
  { 0x030F4, 0x00000000, 0x38A00000 },  // cave tex retry: li r5, 0
  { 0x030F8, 0x00000000, 0x98A6011B },  // cave tex retry: stb r5, 0x11b(r6) (clear the mark)
  { 0x030FC, 0x00000000, 0x80660114 },  // cave tex retry: lwz r3, 0x114(r6) (picture id)
  { 0x03100, 0x00000000, 0x2C03FFFF },  // cave tex retry: cmpwi r3, -1
  { 0x03104, 0x00000000, 0x4182000C },  // cave tex retry: beq next (entry freed)
  { 0x03108, 0x00000000, 0x7E048378 },  // cave tex retry: mr r4, r16
  { 0x0310C, 0x00000000, 0x48054FA5 },  // cave tex retry: bl 0x580b0 (r5 = 0 as the original callers)
  { 0x03110, 0x00000000, 0x3610FFFF },  // cave tex retry: next: addic. r16, r16, -1
  { 0x03114, 0x00000000, 0x4181FFC8 },  // cave tex retry: bgt loop
  { 0x03118, 0x00000000, 0x48000ED4 },  // cave tex retry: b 0x3fec
  { 0x03FEC, 0x00000000, 0x82010004 },  // cave tex retry: lwz r16, 4(r1)
  { 0x03FF0, 0x00000000, 0x80010000 },  // cave tex retry: lwz r0, 0(r1)
  { 0x03FF4, 0x00000000, 0x7C0803A6 },  // cave tex retry: mtlr r0
  { 0x03FF8, 0x00000000, 0x38210010 },  // cave tex retry: addi r1, r1, 0x10
  { 0x03FFC, 0x00000000, 0x480547B0 },  // cave tex retry: b 0x587ac (the original call, cr7 as before)
};

#endif  // INCLUDED_SPIKEOUT8LINK_H
