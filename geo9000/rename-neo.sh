#!/bin/sh
# Rename neo files with rhash

# Copyright 2024 orbea
# All rights reserved.
#
# Redistribution and use of this script, with or without modification, is
# permitted provided that the following conditions are met:
#
# 1. Redistributions of this script must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED
#  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
#  EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
#  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
#  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
#  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -eu

neo_list="name=\"2020 Super Baseball (set 1).neo\" mame=\"2020bb.neo\" crc=\"8041ad52\"
name=\"2020 Super Baseball (set 2).neo\" mame=\"2020bba.neo\" crc=\"e9526781\"
name=\"2020 Super Baseball (set 3).neo\" mame=\"2020bbh.neo\" crc=\"9143727f\"
name=\"3 Count Bout - Fire Suplex (NGM-043 ~ NGH-043).neo\" mame=\"3countb.neo\" crc=\"03bdc5a6\"
name=\"Aero Fighters 2 - Sonic Wings 2.neo\" mame=\"sonicwi2.neo\" crc=\"ac1851bb\"
name=\"Aero Fighters 3 - Sonic Wings 3.neo\" mame=\"sonicwi3.neo\" crc=\"10edc4ff\"
name=\"Aggressors of Dark Kombat - Tsuukai GANGAN Koushinkyoku (ADM-008 ~ ADH-008).neo\" mame=\"aodk.neo\" crc=\"8dcafd3b\"
name=\"Alpha Mission II - ASO II - Last Guardian (NGM-007 ~ NGH-007).neo\" mame=\"alpham2.neo\" crc=\"d8cb49e9\"
name=\"Alpha Mission II - ASO II - Last Guardian (prototype).neo\" mame=\"alpham2p.neo\" crc=\"aab8f811\"
name=\"Andro Dunos (NGM-049 ~ NGH-049).neo\" mame=\"androdun.neo\" crc=\"6aaed441\"
name=\"Art of Fighting - Ryuuko no Ken (NGM-044 ~ NGH-044).neo\" mame=\"aof.neo\" crc=\"02e281f9\"
name=\"Art of Fighting 2 - Ryuuko no Ken 2 (NGH-056).neo\" mame=\"aof2a.neo\" crc=\"c649e151\"
name=\"Art of Fighting 2 - Ryuuko no Ken 2 (NGM-056).neo\" mame=\"aof2.neo\" crc=\"bc3e4c20\"
name=\"Art of Fighting 3 - The Path of the Warrior (Korean release).neo\" mame=\"aof3k.neo\" crc=\"890a3f7a\"
name=\"Art of Fighting 3 - The Path of the Warrior - Art of Fighting - Ryuuko no Ken Gaiden.neo\" mame=\"aof3.neo\" crc=\"46b7eac3\"
name=\"Bakatonosama Mahjong Manyuuki (MOM-002 ~ MOH-002).neo\" mame=\"bakatono.neo\" crc=\"4e1acd2b\"
name=\"Bang Bang Busters (2010 NCI release).neo\" mame=\"b2b.neo\" crc=\"5228faba\"
name=\"Bang Bead.neo\" mame=\"bangbead.neo\" crc=\"0003bb08\"
name=\"Baseball Stars 2.neo\" mame=\"bstars2.neo\" crc=\"cea02498\"
name=\"Baseball Stars Professional (NGH-002).neo\" mame=\"bstarsh.neo\" crc=\"1fff2e59\"
name=\"Baseball Stars Professional (NGM-002).neo\" mame=\"bstars.neo\" crc=\"221e9fdb\"
name=\"Battle Flip Shot.neo\" mame=\"flipshot.neo\" crc=\"e3d2d230\"
name=\"Blazing Star.neo\" mame=\"blazstar.neo\" crc=\"21aa3a79\"
name=\"Blue's Journey - Raguy (ALH-001).neo\" mame=\"bjourneyh.neo\" crc=\"c552ea99\"
name=\"Blue's Journey - Raguy (ALM-001 ~ ALH-001).neo\" mame=\"bjourney.neo\" crc=\"3d0805de\"
name=\"Breakers Revenge.neo\" mame=\"breakrev.neo\" crc=\"0874ff8b\"
name=\"Breakers.neo\" mame=\"breakers.neo\" crc=\"e9e191f2\"
name=\"Burning Fight (NGH-018, US).neo\" mame=\"burningfh.neo\" crc=\"adcc7b7d\"
name=\"Burning Fight (NGM-018 ~ NGH-018).neo\" mame=\"burningf.neo\" crc=\"62c88a2d\"
name=\"Burning Fight (prototype, near final, ver 23.3, 910326).neo\" mame=\"burningfpa.neo\" crc=\"b5df2d4e\"
name=\"Burning Fight (prototype, newer, V07).neo\" mame=\"burningfpb.neo\" crc=\"f208a832\"
name=\"Burning Fight (prototype, older).neo\" mame=\"burningfp.neo\" crc=\"8d3e9c7b\"
name=\"Captain Tomaday.neo\" mame=\"ctomaday.neo\" crc=\"0ce8e4f2\"
name=\"Chibi Maruko-chan: Maruko Deluxe Quiz.neo\" mame=\"marukodq.neo\" crc=\"a725e249\"
name=\"Choutetsu Brikin'ger - Iron Clad (prototype).neo\" mame=\"ironclad.neo\" crc=\"e38554fb\"
name=\"Choutetsu Brikin'ger - Iron Clad (prototype, bootleg).neo\" mame=\"ironclado.neo\" crc=\"300b920a\"
name=\"Crossed Swords (ALM-002 ~ ALH-002).neo\" mame=\"crsword.neo\" crc=\"75fabd5d\"
name=\"Crossed Swords 2 (bootleg of CD version).neo\" mame=\"crswd2bl.neo\" crc=\"9acfb8e5\"
name=\"Crouching Tiger Hidden Dragon 2003 (hack of The King of Fighters 2001).neo\" mame=\"cthd2003.neo\" crc=\"62e9ef46\"
name=\"Crouching Tiger Hidden Dragon 2003 Super Plus (hack of The King of Fighters 2001).neo\" mame=\"ct2k3sp.neo\" crc=\"060956ce\"
name=\"Crouching Tiger Hidden Dragon 2003 Super Plus (hack of The King of Fighters 2001, alternate).neo\" mame=\"ct2k3sa.neo\" crc=\"7e74bdf9\"
name=\"Cyber-Lip (NGM-010).neo\" mame=\"cyberlip.neo\" crc=\"d20d6a3e\"
name=\"Digger Man (prototype).neo\" mame=\"diggerma.neo\" crc=\"41581ae2\"
name=\"Double Dragon (Neo-Geo).neo\" mame=\"doubledr.neo\" crc=\"5f92a3aa\"
name=\"Dragon's Heaven (development board).neo\" mame=\"dragonsh.neo\" crc=\"1e3e2500\"
name=\"Eight Man (NGM-025 ~ NGH-025).neo\" mame=\"eightman.neo\" crc=\"843dcf30\"
name=\"Far East of Eden - Kabuki Klash - Tengai Makyou - Shin Den.neo\" mame=\"kabukikl.neo\" crc=\"e5c7c02f\"
name=\"Fatal Fury - King of Fighters - Garou Densetsu - Shukumei no Tatakai (NGM-033 ~ NGH-033).neo\" mame=\"fatfury1.neo\" crc=\"dabcc80b\"
name=\"Fatal Fury 2 - Garou Densetsu 2 - Arata-naru Tatakai (NGM-047 ~ NGH-047).neo\" mame=\"fatfury2.neo\" crc=\"1a896e27\"
name=\"Fatal Fury 3 - Road to the Final Victory - Garou Densetsu 3 - Haruka-naru Tatakai (NGM-069 ~ NGH-069).neo\" mame=\"fatfury3.neo\" crc=\"d353eeed\"
name=\"Fatal Fury Special - Garou Densetsu Special (NGM-058 ~ NGH-058, set 1).neo\" mame=\"fatfursp.neo\" crc=\"beccef57\"
name=\"Fatal Fury Special - Garou Densetsu Special (NGM-058 ~ NGH-058, set 2).neo\" mame=\"fatfurspa.neo\" crc=\"e2f91cac\"
name=\"Fight Fever - Wang Jung Wang (set 1).neo\" mame=\"fightfev.neo\" crc=\"8515503b\"
name=\"Fight Fever - Wang Jung Wang (set 2).neo\" mame=\"fightfeva.neo\" crc=\"babd8403\"
name=\"Fighters Swords (Korean release of Samurai Shodown III).neo\" mame=\"fswords.neo\" crc=\"e6aa08c1\"
name=\"Football Frenzy (NGM-034 ~ NGH-034).neo\" mame=\"fbfrenzy.neo\" crc=\"2db33ec0\"
name=\"Galaxy Fight - Universal Warriors.neo\" mame=\"galaxyfg.neo\" crc=\"ec905658\"
name=\"Ganryu - Musashi Ganryuki.neo\" mame=\"ganryu.neo\" crc=\"382e661f\"
name=\"Garou - Mark of the Wolves (bootleg).neo\" mame=\"garoubl.neo\" crc=\"2c3351e9\"
name=\"Garou - Mark of the Wolves (NGH-2530).neo\" mame=\"garouha.neo\" crc=\"9f96e440\"
name=\"Garou - Mark of the Wolves (NGM-2530 ~ NGH-2530).neo\" mame=\"garouh.neo\" crc=\"bf2204c4\"
name=\"Garou - Mark of the Wolves (NGM-2530).neo\" mame=\"garou.neo\" crc=\"25d2e39e\"
name=\"Garou - Mark of the Wolves (prototype).neo\" mame=\"garoup.neo\" crc=\"81275c80\"
name=\"Ghost Pilots (NGH-020, US).neo\" mame=\"gpilotsh.neo\" crc=\"eea2b267\"
name=\"Ghost Pilots (NGM-020 ~ NGH-020).neo\" mame=\"gpilots.neo\" crc=\"aad82c6c\"
name=\"Ghost Pilots (prototype).neo\" mame=\"gpilotsp.neo\" crc=\"3b16438b\"
name=\"Ghostlop (prototype).neo\" mame=\"ghostlop.neo\" crc=\"adafd77a\"
name=\"Goal! Goal! Goal!.neo\" mame=\"goalx3.neo\" crc=\"1a3799d6\"
name=\"Gururin.neo\" mame=\"gururin.neo\" crc=\"5c587362\"
name=\"Idol Mahjong Final Romance 2 (Neo-Geo, bootleg of CD version).neo\" mame=\"froman2b.neo\" crc=\"e7d57377\"
name=\"Janshin Densetsu - Quest of Jongmaster.neo\" mame=\"janshin.neo\" crc=\"c62ab999\"
name=\"Jockey Grand Prix (set 1).neo\" mame=\"jockeygp.neo\" crc=\"71db01dd\"
name=\"Jockey Grand Prix (set 2).neo\" mame=\"jockeygpa.neo\" crc=\"23102f16\"
name=\"Karnov's Revenge - Fighter's History Dynamite.neo\" mame=\"karnovr.neo\" crc=\"be78c856\"
name=\"King of Gladiator (bootleg of The King of Fighters '97).neo\" mame=\"kog.neo\" crc=\"890805a0\"
name=\"King of the Monsters (set 1).neo\" mame=\"kotm.neo\" crc=\"03919498\"
name=\"King of the Monsters (set 2).neo\" mame=\"kotmh.neo\" crc=\"a4c35bfd\"
name=\"King of the Monsters 2 - The Next Thing (NGM-039 ~ NGH-039).neo\" mame=\"kotm2.neo\" crc=\"45d1ee0c\"
name=\"King of the Monsters 2 - The Next Thing (older).neo\" mame=\"kotm2a.neo\" crc=\"8244fe15\"
name=\"King of the Monsters 2 - The Next Thing (prototype).neo\" mame=\"kotm2p.neo\" crc=\"2589d82e\"
name=\"Kizuna Encounter - Super Tag Battle - Fu'un Super Tag Battle.neo\" mame=\"kizuna.neo\" crc=\"4a93ef82\"
name=\"Lansquenet 2004 (bootleg of Shock Troopers - 2nd Squad).neo\" mame=\"lans2004.neo\" crc=\"f0bdeb60\"
name=\"Last Hope (bootleg AES to MVS conversion, no coin support).neo\" mame=\"lasthope.neo\" crc=\"16c6ae63\"
name=\"Last Resort (prototype).neo\" mame=\"lresortp.neo\" crc=\"c41b32c3\"
name=\"Last Resort.neo\" mame=\"lresort.neo\" crc=\"6738aeb0\"
name=\"League Bowling (NGM-019 ~ NGH-019).neo\" mame=\"lbowling.neo\" crc=\"7222c3ce\"
name=\"Legend of Success Joe - Ashita no Joe Densetsu.neo\" mame=\"legendos.neo\" crc=\"67f65c54\"
name=\"Magical Drop II.neo\" mame=\"magdrop2.neo\" crc=\"fd5d3e9a\"
name=\"Magical Drop III.neo\" mame=\"magdrop3.neo\" crc=\"7bf1a9b0\"
name=\"Magician Lord (NGH-005).neo\" mame=\"maglordh.neo\" crc=\"f2d54371\"
name=\"Magician Lord (NGM-005).neo\" mame=\"maglord.neo\" crc=\"11971fbf\"
name=\"Mahjong Kyo Retsuden (NGM-004 ~ NGH-004).neo\" mame=\"mahretsu.neo\" crc=\"18309421\"
name=\"Matrimelee - Shin Gouketsuji Ichizoku Toukon (bootleg).neo\" mame=\"matrimbl.neo\" crc=\"96de3019\"
name=\"Matrimelee - Shin Gouketsuji Ichizoku Toukon (NGM-2660 ~ NGH-2660).neo\" mame=\"matrim.neo\" crc=\"b09a3c96\"
name=\"Metal Slug - Super Vehicle-001.neo\" mame=\"mslug.neo\" crc=\"05be3849\"
name=\"Metal Slug 2 - Super Vehicle-001/II (NGM-2410 ~ NGH-2410).neo\" mame=\"mslug2.neo\" crc=\"ac0aa8ef\"
name=\"Metal Slug 2 Turbo (NGM-9410) (hack).neo\" mame=\"mslug2t.neo\" crc=\"2056e958\"
name=\"Metal Slug 3 (NGH-2560).neo\" mame=\"mslug3h.neo\" crc=\"24b483c3\"
name=\"Metal Slug 3 (NGM-2560).neo\" mame=\"mslug3.neo\" crc=\"296f41b1\"
name=\"Metal Slug 3 (NGM-2560, earlier).neo\" mame=\"mslug3a.neo\" crc=\"4d42caf5\"
name=\"Metal Slug 4 (NGH-2630).neo\" mame=\"mslug4h.neo\" crc=\"9a041b23\"
name=\"Metal Slug 4 (NGM-2630).neo\" mame=\"mslug4.neo\" crc=\"80822177\"
name=\"Metal Slug 4 Plus (bootleg).neo\" mame=\"ms4plus.neo\" crc=\"9aec5792\"
name=\"Metal Slug 5 (bootleg, set 1).neo\" mame=\"mslug5b.neo\" crc=\"5d1f010c\"
name=\"Metal Slug 5 (NGH-2680).neo\" mame=\"mslug5h.neo\" crc=\"ba52e48d\"
name=\"Metal Slug 5 (NGM-2680).neo\" mame=\"mslug5.neo\" crc=\"11e4f258\"
name=\"Metal Slug 5 Plus (bootleg).neo\" mame=\"ms5plus.neo\" crc=\"0271c9c4\"
name=\"Metal Slug 6 (bootleg of Metal Slug 3).neo\" mame=\"mslug3b6.neo\" crc=\"f70d50cb\"
name=\"Metal Slug X - Super Vehicle-001 (NGM-2500 ~ NGH-2500).neo\" mame=\"mslugx.neo\" crc=\"fdc65a72\"
name=\"Minasan no Okagesamadesu! Dai Sugoroku Taikai (MOM-001 ~ MOH-001).neo\" mame=\"minasan.neo\" crc=\"fa5f4de4\"
name=\"Money Puzzle Exchanger - Money Idol Exchanger.neo\" mame=\"miexchng.neo\" crc=\"7edfbb25\"
name=\"Mutation Nation (NGM-014 ~ NGH-014).neo\" mame=\"mutnat.neo\" crc=\"100027fa\"
name=\"NAM-1975 (NGM-001 ~ NGH-001).neo\" mame=\"nam1975.neo\" crc=\"338f2dee\"
name=\"Neo Bomberman.neo\" mame=\"neobombe.neo\" crc=\"452609d6\"
name=\"Neo Drift Out - New Technology.neo\" mame=\"neodrift.neo\" crc=\"f2063afc\"
name=\"Neo Mr. Do!.neo\" mame=\"neomrdo.neo\" crc=\"6ab90d70\"
name=\"Neo Turf Masters - Big Tournament Golf.neo\" mame=\"turfmast.neo\" crc=\"f00c2138\"
name=\"Neo-Geo Cup '98 - The Road to the Victory.neo\" mame=\"neocup98.neo\" crc=\"3896f2d9\"
name=\"Nightmare in the Dark (bootleg).neo\" mame=\"nitdbl.neo\" crc=\"ed62a567\"
name=\"Nightmare in the Dark.neo\" mame=\"nitd.neo\" crc=\"42ce8908\"
name=\"Ninja Combat (NGH-009).neo\" mame=\"ncombath.neo\" crc=\"00a23c2a\"
name=\"Ninja Combat (NGM-009).neo\" mame=\"ncombat.neo\" crc=\"75d42338\"
name=\"Ninja Commando.neo\" mame=\"ncommand.neo\" crc=\"817db341\"
name=\"Ninja Master's - Haoh-ninpo-cho.neo\" mame=\"ninjamas.neo\" crc=\"6c043356\"
name=\"Over Top.neo\" mame=\"overtop.neo\" crc=\"935c39ea\"
name=\"Pae Wang Jeon Seol - Legend of a Warrior (Korean censored Samurai Shodown IV).neo\" mame=\"samsho4k.neo\" crc=\"9642929c\"
name=\"Panic Bomber.neo\" mame=\"panicbom.neo\" crc=\"c7799f9d\"
name=\"Pleasure Goal - Futsal - 5 on 5 Mini Soccer (NGM-219).neo\" mame=\"pgoal.neo\" crc=\"463fe2bc\"
name=\"Pochi and Nyaa (Ver 2.00).neo\" mame=\"pnyaaa.neo\" crc=\"aea780c7\"
name=\"Pochi and Nyaa (Ver 2.02).neo\" mame=\"pnyaa.neo\" crc=\"9a371ae5\"
name=\"Pop 'n Bounce - Gapporin.neo\" mame=\"popbounc.neo\" crc=\"30c2f23f\"
name=\"Power Spikes II (NGM-068).neo\" mame=\"pspikes2.neo\" crc=\"7e6bb2eb\"
name=\"Prehistoric Isle 2.neo\" mame=\"preisle2.neo\" crc=\"2734d1be\"
name=\"Pulstar.neo\" mame=\"pulstar.neo\" crc=\"71d34714\"
name=\"Puzzle Bobble - Bust-A-Move (Neo-Geo, bootleg).neo\" mame=\"pbobblenb.neo\" crc=\"54fa6513\"
name=\"Puzzle Bobble - Bust-A-Move (Neo-Geo, NGM-083).neo\" mame=\"pbobblen.neo\" crc=\"71619b37\"
name=\"Puzzle Bobble 2 - Bust-A-Move Again (Neo-Geo).neo\" mame=\"pbobbl2n.neo\" crc=\"1710150c\"
name=\"Puzzle De Pon! R!.neo\" mame=\"puzzldpr.neo\" crc=\"a7ea0a8a\"
name=\"Puzzle De Pon!.neo\" mame=\"puzzledp.neo\" crc=\"9610638a\"
name=\"Puzzled - Joy Joy Kid (NGM-021 ~ NGH-021).neo\" mame=\"joyjoy.neo\" crc=\"2c368d6a\"
name=\"Quiz Daisousa Sen - The Last Count Down (NGM-023 ~ NGH-023).neo\" mame=\"quizdais.neo\" crc=\"483b8670\"
name=\"Quiz King of Fighters (Korea).neo\" mame=\"quizkofk.neo\" crc=\"f660f0d9\"
name=\"Quiz King of Fighters (SAM-080 ~ SAH-080).neo\" mame=\"quizkof.neo\" crc=\"0603f66d\"
name=\"Quiz Meitantei Neo & Geo - Quiz Daisousa Sen part 2 (NGM-042 ~ NGH-042).neo\" mame=\"quizdai2.neo\" crc=\"bd15b7a1\"
name=\"Quiz Salibtamjeong - The Last Count Down (Korean localized Quiz Daisousa Sen).neo\" mame=\"quizdaisk.neo\" crc=\"d0c614a0\"
name=\"Rage of the Dragons (NGH-2640).neo\" mame=\"rotdh.neo\" crc=\"ecf6a9f7\"
name=\"Rage of the Dragons (NGM-2640).neo\" mame=\"rotd.neo\" crc=\"29ae65fd\"
name=\"Ragnagard - Shin-Oh-Ken.neo\" mame=\"ragnagrd.neo\" crc=\"8c08477b\"
name=\"Real Bout Fatal Fury - Real Bout Garou Densetsu (bug fix revision).neo\" mame=\"rbff1a.neo\" crc=\"ec8b37f0\"
name=\"Real Bout Fatal Fury - Real Bout Garou Densetsu (Korean release).neo\" mame=\"rbff1k.neo\" crc=\"bf5b4349\"
name=\"Real Bout Fatal Fury - Real Bout Garou Densetsu (Korean release, bug fix revision).neo\" mame=\"rbff1ka.neo\" crc=\"40eb7054\"
name=\"Real Bout Fatal Fury - Real Bout Garou Densetsu (NGM-095 ~ NGH-095).neo\" mame=\"rbff1.neo\" crc=\"9de2bc23\"
name=\"Real Bout Fatal Fury 2 - The Newcomers (Korean release).neo\" mame=\"rbff2k.neo\" crc=\"d23d0173\"
name=\"Real Bout Fatal Fury 2 - The Newcomers - Real Bout Garou Densetsu 2 - The Newcomers (NGH-2400).neo\" mame=\"rbff2h.neo\" crc=\"fc3be0de\"
name=\"Real Bout Fatal Fury 2 - The Newcomers - Real Bout Garou Densetsu 2 - The Newcomers (NGM-2400).neo\" mame=\"rbff2.neo\" crc=\"5ba5aa03\"
name=\"Real Bout Fatal Fury Special - Real Bout Garou Densetsu Special (Korean release).neo\" mame=\"rbffspeck.neo\" crc=\"da1e9521\"
name=\"Real Bout Fatal Fury Special - Real Bout Garou Densetsu Special.neo\" mame=\"rbffspec.neo\" crc=\"60f8fcf8\"
name=\"Riding Hero (NGM-006 ~ NGH-006).neo\" mame=\"ridhero.neo\" crc=\"4221f383\"
name=\"Riding Hero (set 2).neo\" mame=\"ridheroh.neo\" crc=\"17184baf\"
name=\"Robo Army (NGM-032 ~ NGH-032).neo\" mame=\"roboarmya.neo\" crc=\"77923e2d\"
name=\"Robo Army.neo\" mame=\"roboarmy.neo\" crc=\"23b6caaf\"
name=\"Samurai Shodown - Samurai Spirits (NGH-045).neo\" mame=\"samshoh.neo\" crc=\"20519852\"
name=\"Samurai Shodown - Samurai Spirits (NGM-045).neo\" mame=\"samsho.neo\" crc=\"12cc6bf3\"
name=\"Samurai Shodown II - Shin Samurai Spirits - Haohmaru Jigokuhen (NGM-063 ~ NGH-063).neo\" mame=\"samsho2.neo\" crc=\"9208e903\"
name=\"Samurai Shodown III - Samurai Spirits - Zankurou Musouken (NGH-087).neo\" mame=\"samsho3h.neo\" crc=\"b738594d\"
name=\"Samurai Shodown III - Samurai Spirits - Zankurou Musouken (NGM-087).neo\" mame=\"samsho3.neo\" crc=\"2bf539cf\"
name=\"Samurai Shodown IV - Amakusa's Revenge - Samurai Spirits - Amakusa Kourin (NGM-222 ~ NGH-222).neo\" mame=\"samsho4.neo\" crc=\"d6d86cd8\"
name=\"Samurai Shodown V - Samurai Spirits Zero (bootleg).neo\" mame=\"samsho5b.neo\" crc=\"a9664cde\"
name=\"Samurai Shodown V - Samurai Spirits Zero (NGH-2700).neo\" mame=\"samsho5h.neo\" crc=\"bf03bc84\"
name=\"Samurai Shodown V - Samurai Spirits Zero (NGM-2700, set 1).neo\" mame=\"samsho5.neo\" crc=\"0d34dde4\"
name=\"Samurai Shodown V - Samurai Spirits Zero (NGM-2700, set 2).neo\" mame=\"samsho5a.neo\" crc=\"8a08119e\"
name=\"Samurai Shodown V Special - Samurai Spirits Zero Special (NGH-2720, 1st release, censored).neo\" mame=\"samsh5spho.neo\" crc=\"f649dac3\"
name=\"Samurai Shodown V Special - Samurai Spirits Zero Special (NGH-2720, 2nd release, less censored).neo\" mame=\"samsh5sph.neo\" crc=\"9febe986\"
name=\"Samurai Shodown V Special - Samurai Spirits Zero Special (NGM-2720).neo\" mame=\"samsh5sp.neo\" crc=\"d06d508c\"
name=\"Saulabi Spirits - Jin Saulabi Tu Hon (Korean release of Samurai Shodown II, set 1).neo\" mame=\"samsho2k.neo\" crc=\"800c26b1\"
name=\"Saulabi Spirits - Jin Saulabi Tu Hon (Korean release of Samurai Shodown II, set 2).neo\" mame=\"samsho2ka.neo\" crc=\"8ba0db81\"
name=\"Savage Reign - Fu'un Mokushiroku - Kakutou Sousei.neo\" mame=\"savagere.neo\" crc=\"68964a13\"
name=\"Sengoku - Sengoku Denshou (NGH-017, US).neo\" mame=\"sengokuh.neo\" crc=\"dbcede61\"
name=\"Sengoku - Sengoku Denshou (NGM-017 ~ NGH-017).neo\" mame=\"sengoku.neo\" crc=\"5c7b047b\"
name=\"Sengoku 2 - Sengoku Denshou 2.neo\" mame=\"sengoku2.neo\" crc=\"e48c07d0\"
name=\"Sengoku 3 - Sengoku Densho 2001 (set 1).neo\" mame=\"sengoku3.neo\" crc=\"311a9ad5\"
name=\"Sengoku 3 - Sengoku Densho 2001 (set 2).neo\" mame=\"sengoku3a.neo\" crc=\"207c2770\"
name=\"Shock Troopers (set 1).neo\" mame=\"shocktro.neo\" crc=\"7e919117\"
name=\"Shock Troopers (set 2).neo\" mame=\"shocktroa.neo\" crc=\"dc4edbe3\"
name=\"Shock Troopers - 2nd Squad.neo\" mame=\"shocktr2.neo\" crc=\"1438e16f\"
name=\"Shougi no Tatsujin - Master of Shougi.neo\" mame=\"moshougi.neo\" crc=\"71112101\"
name=\"SNK vs. Capcom - SVC Chaos (bootleg).neo\" mame=\"svcboot.neo\" crc=\"83ee63a1\"
name=\"SNK vs. Capcom - SVC Chaos (NGM-2690 ~ NGH-2690).neo\" mame=\"svc.neo\" crc=\"7376fc43\"
name=\"SNK vs. Capcom - SVC Chaos Plus (bootleg set 1).neo\" mame=\"svcplus.neo\" crc=\"83a48e6b\"
name=\"SNK vs. Capcom - SVC Chaos Plus (bootleg set 2).neo\" mame=\"svcplusa.neo\" crc=\"e06d4da4\"
name=\"SNK vs. Capcom - SVC Chaos Super Plus (bootleg).neo\" mame=\"svcsplus.neo\" crc=\"30e60a48\"
name=\"Soccer Brawl (NGH-031).neo\" mame=\"socbrawlh.neo\" crc=\"02d3b361\"
name=\"Soccer Brawl (NGM-031).neo\" mame=\"socbrawl.neo\" crc=\"2ea06384\"
name=\"Spin Master - Miracle Adventure.neo\" mame=\"spinmast.neo\" crc=\"6c76c571\"
name=\"Stakes Winner - Stakes Winner - GI Kinzen Seiha e no Michi.neo\" mame=\"stakwin.neo\" crc=\"fb42bea3\"
name=\"Stakes Winner 2.neo\" mame=\"stakwin2.neo\" crc=\"9c791c8c\"
name=\"Street Hoop - Street Slam - Dunk Dream (DEM-004 ~ DEH-004).neo\" mame=\"strhoop.neo\" crc=\"fa3f7ee1\"
name=\"Strikers 1945 Plus.neo\" mame=\"s1945p.neo\" crc=\"a5f8886e\"
name=\"Super Bubble Pop (prototype).neo\" mame=\"sbp.neo\" crc=\"346afd7f\"
name=\"Super Dodge Ball - Kunio no Nekketsu Toukyuu Densetsu.neo\" mame=\"sdodgeb.neo\" crc=\"04e50958\"
name=\"Super Sidekicks - Tokuten Ou.neo\" mame=\"ssideki.neo\" crc=\"99b48cbd\"
name=\"Super Sidekicks 2 - The World Championship - Tokuten Ou 2 - Real Fight Football (NGM-061 ~ NGH-061).neo\" mame=\"ssideki2.neo\" crc=\"51744243\"
name=\"Super Sidekicks 3 - The Next Glory - Tokuten Ou 3 - Eikou e no Chousen.neo\" mame=\"ssideki3.neo\" crc=\"68b96364\"
name=\"Tecmo World Soccer '96.neo\" mame=\"twsoc96.neo\" crc=\"dd9c0acf\"
name=\"The Irritating Maze - Ultra Denryu Iraira Bou.neo\" mame=\"irrmaze.neo\" crc=\"c03d0ec0\"
name=\"The King of Fighters '94 (NGM-055 ~ NGH-055).neo\" mame=\"kof94.neo\" crc=\"1de3c0cb\"
name=\"The King of Fighters '95 (NGH-084).neo\" mame=\"kof95h.neo\" crc=\"a2b60b1d\"
name=\"The King of Fighters '95 (NGM-084).neo\" mame=\"kof95.neo\" crc=\"161c3f78\"
name=\"The King of Fighters '95 (NGM-084, alt board).neo\" mame=\"kof95a.neo\" crc=\"74fad1b2\"
name=\"The King of Fighters '96 (NGH-214).neo\" mame=\"kof96h.neo\" crc=\"1be5ef6e\"
name=\"The King of Fighters '96 (NGM-214).neo\" mame=\"kof96.neo\" crc=\"e54b8545\"
name=\"The King of Fighters '97 (Korean release).neo\" mame=\"kof97k.neo\" crc=\"4c007242\"
name=\"The King of Fighters '97 (NGH-2320).neo\" mame=\"kof97h.neo\" crc=\"f04f9dc7\"
name=\"The King of Fighters '97 (NGM-2320).neo\" mame=\"kof97.neo\" crc=\"bef7053f\"
name=\"The King of Fighters '97 Chongchu Jianghu Plus 2003 (bootleg, set 1).neo\" mame=\"kof97oro.neo\" crc=\"7e41756c\"
name=\"The King of Fighters '97 Plus (bootleg).neo\" mame=\"kof97pls.neo\" crc=\"8ca84974\"
name=\"The King of Fighters '98 - The Slugfest - King of Fighters '98 - Dream Match Never Ends (Korean board, set 1).neo\" mame=\"kof98k.neo\" crc=\"79034090\"
name=\"The King of Fighters '98 - The Slugfest - King of Fighters '98 - Dream Match Never Ends (Korean board, set 2).neo\" mame=\"kof98ka.neo\" crc=\"f90c3ac7\"
name=\"The King of Fighters '98 - The Slugfest - King of Fighters '98 - Dream Match Never Ends (NGH-2420).neo\" mame=\"kof98h.neo\" crc=\"b5add56b\"
name=\"The King of Fighters '98 - The Slugfest - King of Fighters '98 - Dream Match Never Ends (NGM-2420).neo\" mame=\"kof98.neo\" crc=\"7937a3b5\"
name=\"The King of Fighters '98 - The Slugfest - King of Fighters '98 - Dream Match Never Ends (NGM-2420, alt board).neo\" mame=\"kof98a.neo\" crc=\"097f43f3\"
name=\"The King of Fighters '99 - Millennium Battle (earlier).neo\" mame=\"kof99e.neo\" crc=\"a340318c\"
name=\"The King of Fighters '99 - Millennium Battle (Korean release).neo\" mame=\"kof99k.neo\" crc=\"bebdef97\"
name=\"The King of Fighters '99 - Millennium Battle (Korean release, non-encrypted program).neo\" mame=\"kof99ka.neo\" crc=\"e683d204\"
name=\"The King of Fighters '99 - Millennium Battle (NGH-2510).neo\" mame=\"kof99h.neo\" crc=\"7068d605\"
name=\"The King of Fighters '99 - Millennium Battle (NGM-2510).neo\" mame=\"kof99.neo\" crc=\"0fa6c82c\"
name=\"The King of Fighters '99 - Millennium Battle (prototype).neo\" mame=\"kof99p.neo\" crc=\"f256c4ec\"
name=\"The King of Fighters 10th Anniversary (bootleg of The King of Fighters 2002).neo\" mame=\"kof10th.neo\" crc=\"b1ebae6e\"
name=\"The King of Fighters 10th Anniversary 2005 Unique (bootleg of The King of Fighters 2002).neo\" mame=\"kf2k5uni.neo\" crc=\"7230b2e9\"
name=\"The King of Fighters 10th Anniversary Extra Plus (bootleg of The King of Fighters 2002).neo\" mame=\"kf10thep.neo\" crc=\"6eeee6bc\"
name=\"The King of Fighters 2000 (NGM-2570 ~ NGH-2570).neo\" mame=\"kof2000.neo\" crc=\"39e509d4\"
name=\"The King of Fighters 2000 (not encrypted).neo\" mame=\"kof2000n.neo\" crc=\"e5f0a167\"
name=\"The King of Fighters 2001 (NGH-2621).neo\" mame=\"kof2001h.neo\" crc=\"9ebbb90a\"
name=\"The King of Fighters 2001 (NGM-262).neo\" mame=\"kof2001.neo\" crc=\"01202e76\"
name=\"The King of Fighters 2002 (bootleg).neo\" mame=\"kof2002b.neo\" crc=\"04845fd1\"
name=\"The King of Fighters 2002 (NGM-2650 ~ NGH-2650).neo\" mame=\"kof2002.neo\" crc=\"388ce6f9\"
name=\"The King of Fighters 2002 Magic Plus (bootleg).neo\" mame=\"kf2k2mp.neo\" crc=\"dbe7f03c\"
name=\"The King of Fighters 2002 Magic Plus II (bootleg).neo\" mame=\"kf2k2mp2.neo\" crc=\"607e6a06\"
name=\"The King of Fighters 2002 Plus (bootleg set 1).neo\" mame=\"kf2k2pls.neo\" crc=\"8974e46d\"
name=\"The King of Fighters 2002 Plus (bootleg set 2).neo\" mame=\"kf2k2pla.neo\" crc=\"b5f1403f\"
name=\"The King of Fighters 2003 (bootleg set 1).neo\" mame=\"kf2k3bl.neo\" crc=\"f9a1c5cb\"
name=\"The King of Fighters 2003 (bootleg set 2).neo\" mame=\"kf2k3bla.neo\" crc=\"4af506bf\"
name=\"The King of Fighters 2003 (NGH-2710).neo\" mame=\"kof2003h.neo\" crc=\"ced0f3fa\"
name=\"The King of Fighters 2003 (NGM-2710, Export).neo\" mame=\"kof2003.neo\" crc=\"c5e8f3ee\"
name=\"The King of Fighters 2004 Plus - Hero (bootleg of The King of Fighters 2003).neo\" mame=\"kf2k3pl.neo\" crc=\"2ec1f4a3\"
name=\"The King of Fighters 2004 Ultra Plus (bootleg of The King of Fighters 2003).neo\" mame=\"kf2k3upl.neo\" crc=\"e49b4a2f\"
name=\"The King of Fighters Special Edition 2004 (bootleg of The King of Fighters 2002).neo\" mame=\"kof2k4se.neo\" crc=\"2dc44424\"
name=\"The Last Blade - Bakumatsu Roman - Gekka no Kenshi (NGH-2340).neo\" mame=\"lastbladh.neo\" crc=\"b2ce2dbb\"
name=\"The Last Blade - Bakumatsu Roman - Gekka no Kenshi (NGM-2340).neo\" mame=\"lastblad.neo\" crc=\"c1c6d0f0\"
name=\"The Last Blade 2 - Bakumatsu Roman - Dai Ni Maku Gekka no Kenshi (NGM-2430 ~ NGH-2430).neo\" mame=\"lastbld2.neo\" crc=\"315305ee\"
name=\"The Last Soldier (Korean release of The Last Blade).neo\" mame=\"lastsold.neo\" crc=\"e7f4e875\"
name=\"The Super Spy (NGM-011 ~ NGH-011).neo\" mame=\"superspy.neo\" crc=\"9f7f80cf\"
name=\"The Ultimate 11 - The SNK Football Championship - Tokuten Ou - Honoo no Libero.neo\" mame=\"ssideki4.neo\" crc=\"574c4b4e\"
name=\"Thrash Rally (ALM-003 ~ ALH-003).neo\" mame=\"trally.neo\" crc=\"ad8dc774\"
name=\"Top Hunter - Roddy & Cathy (NGH-046).neo\" mame=\"tophuntrh.neo\" crc=\"661180a9\"
name=\"Top Hunter - Roddy & Cathy (NGM-046).neo\" mame=\"tophuntr.neo\" crc=\"bc747b28\"
name=\"Top Player's Golf (NGM-003 ~ NGH-003).neo\" mame=\"tpgolf.neo\" crc=\"f4a8abc6\"
name=\"Twinkle Star Sprites.neo\" mame=\"twinspri.neo\" crc=\"192ccf78\"
name=\"V-Liner (v0.53).neo\" mame=\"vliner53.neo\" crc=\"94aa40bd\"
name=\"V-Liner (v0.54).neo\" mame=\"vliner54.neo\" crc=\"6bc89cd6\"
name=\"V-Liner (v0.6e).neo\" mame=\"vliner6e.neo\" crc=\"a7bdda97\"
name=\"V-Liner (v0.7a).neo\" mame=\"vliner.neo\" crc=\"7c3c125a\"
name=\"V-Liner (v0.7e).neo\" mame=\"vliner7e.neo\" crc=\"013a7536\"
name=\"Viewpoint (prototype).neo\" mame=\"viewpoinp.neo\" crc=\"52867517\"
name=\"Viewpoint.neo\" mame=\"viewpoin.neo\" crc=\"110f9248\"
name=\"Voltage Fighter - Gowcaizer - Choujin Gakuen Gowcaizer.neo\" mame=\"gowcaizr.neo\" crc=\"519b1bd4\"
name=\"Waku Waku 7.neo\" mame=\"wakuwak7.neo\" crc=\"81b7ad16\"
name=\"Windjammers - Flying Power Disc.neo\" mame=\"wjammers.neo\" crc=\"342f7f79\"
name=\"World Heroes (ALH-005).neo\" mame=\"wh1h.neo\" crc=\"e65d03d1\"
name=\"World Heroes (ALM-005).neo\" mame=\"wh1.neo\" crc=\"d864f492\"
name=\"World Heroes (set 3).neo\" mame=\"wh1ha.neo\" crc=\"9ff1ed75\"
name=\"World Heroes 2 (ALH-006).neo\" mame=\"wh2h.neo\" crc=\"20d3e23d\"
name=\"World Heroes 2 (ALM-006 ~ ALH-006).neo\" mame=\"wh2.neo\" crc=\"99e14fc1\"
name=\"World Heroes 2 Jet (ADM-007 ~ ADH-007).neo\" mame=\"wh2j.neo\" crc=\"e1a45894\"
name=\"World Heroes Perfect.neo\" mame=\"whp.neo\" crc=\"37858a01\"
name=\"Zed Blade - Operation Ragnarok.neo\" mame=\"zedblade.neo\" crc=\"4cc9e910\"
name=\"Zintrick - Oshidashi Zentrix (bootleg of CD version).neo\" mame=\"zintrckb.neo\" crc=\"5cea990f\"
name=\"Zupapa!.neo\" mame=\"zupapa.neo\" crc=\"6f323821\""

die () {
  ret="$1"; shift
  case "$ret" in
    : ) printf %s\\n "$@" >&2; return 0 ;;
    0 ) printf %s\\n "$@" ;;
    * ) printf %s\\n "$@" >&2 ;;
  esac
  exit "$ret"
}

handleargs () {
  case "$1" in
    -- ) return 1 ;;
    -h|--help ) die 0 "$usage" ;;
    -m|--mame ) MAME=1 ;;
    -n|--dryrun ) DRYRUN=1 ;;
    --* ) die 1 "$PRGNAM: Unrecognized option '$1'" "$help" ;;
    -* ) shortargs "${1#-}" ;;
    * ) die 1 "$PRGNAM: Unrecognized option '$1'" "$help" ;;
  esac
}

shortargs () {
  arg="$1"
  while [ -n "${arg}" ]; do
    flag="$(printf %s "${arg%"${arg#?}"}")"
    arg="$(printf %s "${arg#?}")"
    case "$flag" in
      h ) option=help ;;
      m ) option=mame ;;
      n ) option=dryrun ;;
      * ) die 1 "$PRGNAM: Unrecognized option -- '$flag'" "$help" ;;
    esac
    handleargs "--$option"
  done
}

DRYRUN=
MAME=
PRGNAM='rename-neo.sh'

help="Try '$PRGNAM --help' for more information."

usage="$PRGNAM - Rename neo files with rhash.

  Usage: $PRGNAM [-hmn]
    -h, --help,   Show this help message.
    -m, --mame,   Rename using the shortened MAME style.
    -n, --dryrun, Enable a test run without renaming files."

while [ $# -gt 0 ]; do
  handleargs "$@" || break
  shift
done

if [ -n "${MAME}" ]; then
  name=mame
  next=crc
else
  name=name
  next=mame
fi

for neo in *.neo; do
  [ -f "${neo}" ] || continue

  crc32="$(rhash -C --simple -- "$neo")"
  new="$(printf %s "$neo_list" | grep "${crc32%% *}" |
    sed -e "s/.*${name}=\"\(.*\)\" ${next}=.*/\1/")"

  if [ -z "${new}" ]; then
    die : "WARNING: no match for '$neo'"
    continue
  fi

  case "$new" in
    */* ) new="$(printf %s "$new" | tr '/' '-')" ;;
  esac

  if [ ${#new} -gt 255 ]; then
    die : 'WARNING: File name is too long.' "Shortening '$new'."
    new="$(printf %s "$new" | cut -c-250).neo"
  fi

  if [ "$neo" = "$new" ]; then
    printf 'Skipping %s\n' "'$neo'"
  elif [ -n "${DRYRUN}" ]; then
    printf '\055> %s\n' "$new"
  else
    mv -- "$neo" "$new"
  fi
done
