OpenMW EldenCombat component

combat.cpp/combat.hpp adapt the public MIT-licensed state-machine design in
ELDEN-RING-Combat-Rewrite/src/sim/{player,data,dummy}.rs into standalone C++20.
Keep LICENSE-MIT.txt with distributions containing this component. OpenMW's
overall GPL licensing continues to apply to the combined application.

The source recreation requires generated src/sim/extracted.rs, animations and
other assets produced from a local Elden Ring installation. Those generated
files are not committed with the recreation. Until the generated timing table
is imported, this component provides documented fallback action timings and
weapon profiles rather than claiming that its full action table, animation or
weapon geometry matches Elden Ring. scripts/import_eldenring_timings.py imports
the local generated table into imported_timings.hpp; CMake enables that table
when the generated header exists and OPENMW_ELDENCOMBAT_COMPILED_TIMINGS is ON.
Redistributable engine builds set this option OFF. They load local action data
from resources/vfs/animations/eldencombat/action_timings.bin beside the bank.
Generate it from the user's own extraction with:
  python scripts/import_eldenring_timings.py path/to/extracted.rs --runtime-only --runtime-output path/to/package/resources/vfs/animations/eldencombat/action_timings.bin
The runtime file is proprietary game-derived data too and must stay local.
timingdata.hpp validates its version, identifiers, finite values, hit windows,
record/sample limits and landing phases before installing the immutable table.
Runtime data takes precedence over a compiled table. Missing or invalid data
retains compiled timing data when available, otherwise authored fallback timing.
Per-action weapon speed, root motion, contacts and landing clocks are identical
to the local generated-header build. No game data is embedded in the reader.
The importer also keeps each action's baked clip identifier and each weapon's
one-/two-handed stance categories. animations.cpp reads the local ERAN bank
from the recreation's tools/bake_anims.py, validates its layout and samples
the original 30-fps model-space joint positions and oriented joint axes.
The engine shares this immutable bank and retargets poses onto actor skeletons.
One- and two-handed stance gait clips are baked, including one-handed heavy and
spear carries. locomotionNeedsCarry() requests an idle arm overlay only when a
one-handed gait falls back to a different category, preserving authored arm
motion in the weapon's own walk/run/sprint loops.
Roll/backstep costs, known roll distances/i-frame lengths, the normalized
stamina estimates, guard raise delay and counter window come from the supplied
code/tests. Light/heavy motion values and the default light chain are retained
where the supplied behavior tests specify them. Other timings and the root
displacement interpolation are OpenMW fallback estimates.

Implemented mechanisms:
  - Input windows, one-slot newest-input buffering and action cancel windows.
  - Light chains, charge/release heavy attacks and alternating heavy chains.
  - Running, roll, backstep, crouch, airborne and guard-counter openers.
  - Imported jump attacks transition to full/short landing recovery on ground
    contact, retaining prepaid stamina and preventing repeated airborne hits.
  - Left-hand and paired chains with independent multi-hit windows and costs.
    Primary light uses the right weapon; the guard/off-hand button selects
    the paired moveset for matching weapons. Each has its own cancel windows.
  - Right- and left-hand two-handed grips, including the shield attack profile.
  - Authored grip-change phases with delayed hand selection and buffered recovery.
  - Spear shield pokes with an independent blocking left arm and one right-hand hit.
  - Release-to-roll/hold-to-sprint, load-dependent dodge displacement and i-frames.
  - Delayed frontal guarding, two-handed guarding, guard breaks and counters.
  - Per-hit stamina payments, out-of-combat free sprinting, exhaustion/rearming,
    reduced guard regeneration and hit-stop.
  - Ground hit reactions and the dummy's accumulated stance damage/recovery.

Critical vulnerability after stance/guard breaks, critical attacks and the
curated boss melee arts are OpenMW integration extensions. Parrying and Elden
Ring weapon skills are absent from the source recreation. bossarts.hpp adds
interruptible tells, one swing contact and cooldowns; the engine applies
native Morrowind spell effects/sounds without altering ordinary attack timings
or charging stamina twice. Guile's Hunter's Thrust requests Action::SpearThrust
from idle through the AI-only mSpearThrust button. It reuses the two-handed
Spear Light1 definition at 1.6x speed with one contact and full recovery;
pose, hit and root-motion clocks remain synchronized. Its 0.25s tell, 12s
cooldown and 30 percent selection chance make it an occasional opener.
Unsupported/native rigs keep Hunter's Rend. The boss art state is transient
and owned per actor.
Guarded ordinary one-handed chains enter Deflected, cancel remaining windows
and queued inputs, and give native recoil ownership for an 18-frame recovery.
GuardHit retains imported guard arm layers; Deflected does not. Two-handed,
heavy/contextual swings and native creature attacks keep authored follow-through. Physics, target selection,
actual weapon contact, health, equipment and animation are owned by OpenMW;
the component receives engine decisions instead of embedding a second world.

grip.hpp preserves the neighboring reference's common grip swaps: five frames
of preparation, hand selection at frame three, then a seventeen-frame completion
clip whose first seven frames lock actions. The adapter owns equipment and the
selected hand; GripTransition only reports the apply event and source clip/clock.
Input::mHandsBusy buffers attacks/dodges until recovery finishes and prevents
guarding or sprinting while the hands change grip. Changes do not reset combat
state or native stamina. Off-hand light attacks use mLeftSpeedMultiplier; paired
and primary attacks retain the selected primary item's mSpeedMultiplier.

Action::GuardThrust adapts the bank's one-handed Spear/GreatSpear Light1 pose and
timing with a shield-only left-arm overlay; the supplied source has no dedicated
shield-poke definition. Holding guard and pressing light with a shield and a
one-handed right spear selects it. The attack retains the usual guard raise
delay, frontal block checks and shield stamina loss, with one native thrust hit
and the ordinary attack cost. A successful block preserves the committed thrust;
guard break, rear/unblockable hits and other interruptions retain normal behavior.
Releasing guard lowers defense even during hit-stop. The shield poke has no
authored forward root movement; ordinary two-handed spear attacks remain separate.

Adapter contract:
  1. Configure equipment/load/guard eligibility and setVitals(current,max).
     Costs and regeneration scale by max/100 so the native fatigue bar is used.
     For a native creature swing, optionally supply mNativeAttack before update:
     its group name, duration and impact frame are read from native text keys.
     The action captures these values in mNativeAnimation and its definition;
     later configuration changes cannot retime the active swing. The native
     definition has one impact, full recovery before cancellation, no imported
     root movement or charge/release restart. Invalid timing falls back to the
     existing definition. No native option preserves the imported action path.
  2. Call update(input,seconds) once per simulation update and copy stamina()
     back to native fatigue. Clear Input edges after the update. Native fatigue
     regeneration/drain must be suppressed when this controller owns fatigue.
     Set mFlying for controlled levitation separately from true mGrounded
     contact. Flight permits ordinary melee chains, guarding and regeneration,
     while preventing new jumps, dodges, sprinting and combat root displacement.
     Existing jumps and airborne swings finish without waiting for touchdown;
     an imported aerial swing retains its hits and prepaid stamina while it
     advances to recovery at the end of its airborne phase.
  3. Use Update action flags and definition() to drive animation.
     An imported definition's mAnimation names the ERAN clip. Sample it at
     frame()*mAnimationSpeed: imported weapon timings have been divided by
     weapon speed, and this factor restores the original authored frame.
     A nonempty mNativeAnimation instead selects the captured native group at
     native speed. Synchronize that track to frame()/30 after update; a new
     action may have started partway through a slow update. Keep native melee
     hit callbacks suppressed, since activeHit() owns its single impact too.
     Jump-attack landing changes the definition and resets its frame without
     starting a new attack sequence. Sample the current definition each update
     so the airborne clip gives way to the full or short landing recovery.
     stanceAnimation(weapon,twoHanded,id) gives the extracted idle/guard/movement
     category. With no local imported table it selects the base category.
     mAdvancedSeconds is the portion of the update outside hit-stop; advance
     animations by this time and scale native velocity by its fraction of the
     world update. mFrozen means the entire update was paused. mJumpStarted
     retains an accepted jump even if its action ends during a long update;
     use it to launch native physics, without charging jump stamina again.
     mDisplacementX/Y are actor-local horizontal displacement in metres, to
     convert through the engine's world scale and collision-controlled motion.
     Imported mRootMotion samples are cumulative [left,up,forward] metres at
     30 fps. rootMotionAt() interpolates at frame*mRootMotionSpeed; horizontal
     actor coordinates negate left and retain forward. Canonical Front roll
     motion rotates toward the captured dodge direction. Backstep retains its
     authored signed motion. Vertical movement and airborne motion remain
     owned by OpenMW physics. Fallback definitions use the estimated curve.
  4. Query activeHit() for contact. A window crossed by a slow frame is retained
     until the next update, even if the action ended. markHit() consumes exactly
     one window; loop to handle multiple windows crossed by the same update.
     A missed live window can be queried again next update until it expires.
     Call deferHit() on a miss and continue through the other windows in this
     update, so a short right weapon cannot prevent a longer left weapon hitting.
     attack() reports the current action's first-hit metadata for animation use;
     activeHit() supplies the actual hit's sequence, index and damage metadata.
  5. receiveHit() reports evade/block/guard break/reaction before engine health
     damage. Incoming stamina/stance damage is normalized; damage itself stays
     in native health units and is never applied by this component. The adapter
     checks target reach and sets mCriticalAvailable before requesting a critical.
     consumeCritical() reserves the target's vulnerability exactly once.
     Paired fallback hits alternate hands. Imported hits set mLeftHand and
     mHasHandAssignment so repeated or reversed hand order stays authoritative.
     Two-handed contact ownership always uses the selected inventory item,
     even when the authored fist/claw animation strikes with the other hand.
     For a left-handed two-hand grip, configure mWeaponClass/mSpeedMultiplier
     from the selected left item and set both mTwoHanded and mTwoHandedLeft.
     Primary attacks and guard counters then report left-hand hit ownership.
  6. applyHitStop(seconds) freezes action/regen/movement after valid contact;
     setDead()/reset() synchronize death, resurrection and loaded-game changes.
     cancelInputActions() releases menus' input ownership: buffered requests,
     pending hit windows and current attacks are discarded without refunding
     stamina, restoring stance or consuming critical vulnerability. Ongoing
     defensive reactions and dodge/jump movement remain paused until resumed.

Native Morrowind armor shields and lights have no weapon damage ranges. Their
two-handed/left-hand attacks use native hand-to-hand health damage with the
imported shield/torch timing, stamina and motion-value profile. Native attack
values and contact geometry remain OpenMW adaptations. The baked animation
bank contains no replacement character mesh; the renderer uses the actor's
existing skeleton and equipment. Without usable bank/skeleton data, visual
playback falls back to the native clips.
The engine adapter also leaves Almalexia on native poses and movement outside
combat so her Tribunal speech retains its authored idle pacing; her AI combat
sequence enables the controller and imported poses for the duel.

Guile's validated Hircine rig selects WeaponClass::Spear and two-handed input
despite having no equipment inventory. This uses the imported Short Spear
actions at source speed; the native creature still owns damage and loot.
Its embedded spear attachment must also validate before humanoid posing is
enabled. The engine pose wrapper skips equipment lookup for creatures without
an inventory, including Guile. Incompatible rigs/assets keep native attack timing. The other Hircine
forms retain native animation. The renderer places the frontmost palm 40 percent up
from the butt using each native mesh's shaft and both displayed palms. A rear
palm floor at 4 percent keeps both hands on short shafts during wide poses,
allowing only the necessary forward adjustment. Native attachments are restored
when imported animation ends. Admission accepts both r/hircine.nif and the
r/xhircine.nif variant selected by the native animation loader. The six curated
humanoid creature model families also accept their exact animated variants.
Imported creature movement retains the native forward-gait/walk root velocity
ratio (Guile runs about 4.87 times his walk speed), also driving the imported
stride clock. NPC movement and native flight/swimming/sneak stay unchanged.
These changes require no new animation bank.

The independent Game setting "disable paralysis" sets paralysis effect duration
and remaining time to zero for all actors, including existing and constant
effects. Native effect cleanup ends only paralysis; other effects in a mixed
spell remain intact. The setting defaults to false; ordinary paralysis checks
are unchanged and expired effects do not resume when it is turned off.

spellcasting.hpp selects imported gestures from a captured native range and
semantic effect classification: restoration, area restoration, support, spectral,
fire, frost and shock. The engine adapter chooses the highest-cost effect within
the first effect's native range at cast start, for spells and activated enchanted
items alike. Native text keys own release/consumption; the helper only fits the
preparation and release/recovery poses to that timeline. Missing source phases
or invalid native keys request native fallback. Moving/first-person casts use
upper-body poses. Actual-bank checks live in CI/eldenring-test-spellcasting.cpp.

Optional poise is separate from stance-break vulnerability and never absorbs
health damage. The world adapter computes non-player resistance from modified
Endurance/Strength plus NPC armor or creature maximum health/level; the pure
poiseFromStats helper caps the baseline at 45. A positive mMaximumPoise lets
weak hits reduce stance/poise while preserving the current action, buffered
requests and pending contacts. Depletion, explicit knockdown or criticals
interrupt; stance breaks retain their existing independent threshold. Poise
refills after mPoiseRecoveryDelay without damage and on ordinary stagger;
configure preserves its remaining fraction, so frame-by-frame native config
updates cannot replenish it. A zero maximum retains prior hit reactions.

mDodgeStaminaMultiplier scales start costs for rolls, crouch rolls and backsteps
once when selecting either imported or fallback definitions. Zero allows a
free dodge at zero stamina; other costs, geometry and timing stay unchanged.
The native adapter exposes poise and dodge controls through launcher settings.

AiTactics::update still owns finite chains and recovery pauses. The native AI
uses meleeAttackFacing for a 60-degree windup cone independent of smooth-turn
completion, so continuous circling cannot starve attacks merely by staying
outside the native 3-degree steering tolerance. Native reach, height, visibility
and hit-contact geometry remain authoritative. aitests.cpp simulates circling
at 30/60/144 fps in both directions with fallback and imported controller data.
Native spell/projectile windups use rangedAttackFacing (30-degree yaw/pitch
cone); native charge release and cooldown keep advancing during turning.
Engine projectile aim/release/consumption and the setting-off path stay native.

All timing uses animation frames at 30 fps. update subdivides positive time
into at most 1/60-second slices and caps one update at one second to avoid huge
catch-up after pauses. Timers, costs and crossed-hit events use those slices.

Standalone checks (no OpenMW dependencies):
  c++ -std=c++20 -O2 -Wall -Wextra -Werror combat.cpp tests.cpp -o /tmp/eldencombat-tests
  /tmp/eldencombat-tests
  c++ -std=c++20 -O2 -Wall -Wextra -Werror animations.cpp animationtests.cpp -o /tmp/eldenanimation-tests
  /tmp/eldenanimation-tests [path/to/local/player_anims.bin]

The CMake eldencombat-imported-tests executable also accepts a local animation
bank path to check that every imported action and weapon idle/guard stance
references a clip present in that bank.
eldentimingdata-tests accepts a local action_timings.bin to check runtime action
selection without a compiled table. eldentimingdata-imported-tests compares that
file against every compiled action/weapon/grip/load/speed combination, including
root samples, both landing phases, hand assignments, costs and damage metadata.
Both suites reject malformed synthetic files and verify fallback restoration.

Bow poses use bow.hpp to fit the imported longbow draw/held/release sequence
to native shoot text keys. The helper distinguishes a continuous release
section from a separate follow-through section, validates all required keys
and clip lengths, and leaves native arrow release, ammo, strength and damage
to OpenMW. Native bows select the a014 carry stance independently of melee
weapon classes. The eldenbow suite covers the phase mapping and accepts a
local player_anims.bin for actual source-pose checks.
