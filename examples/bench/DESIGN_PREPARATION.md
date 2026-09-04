# Single-spool turbojet engineering preparation

Status: **DEVELOPMENT / Gate 0 open**. The preserved R3 geometry is a packaging
baseline, not a released rotating assembly. No fabricated or fueled operation is
authorized until the adjacent evidence manifest reaches `GROUND_TEST_RELEASED`.

## 1. Purpose and operating conditions

- **Given:** a manufacturable, horizontal, single-spool turbojet mechanical assembly with six axial compressor stages and one turbine stage; it must be an engineered CAD model, not a visual scene.
- **Given:** the target is a contained, instrumented ground demonstrator in Morocco using EASA CS-E Amendment 8 as a development-assurance framework.
- **Open:** aerodynamic, thermal, rotor-dynamic, fatigue, creep, burst, containment, controls, fuel, oil, ignition, fire-safety, test-cell, and authority substantiation. These require qualified partner evidence; ICAD must not synthesize them.
- **Assumed:** nominal dry shop environment for inspection; service access is from split casing flanges, top combustor cover, front/rear bearing stations, and bolted module joints.
- **Failure modes represented geometrically:** rotor/casing rub (radial gaps), bearing mis-seat (named interfaces), fastener edge-distance failure, liner perforation breakout, module flange misalignment, and inaccessible retaining hardware.
- **Historical benchmark targets only:** approximately 13.3 kN static thrust, 3.6:1 compressor pressure ratio, 950 °C turbine inlet temperature, 96,000 rpm maximum speed, and 85 kg dry mass. They are not requirements or operating limits. Qualified cycle, stress, thermal, and rotor-dynamic work will establish defensible values. Jet A-1 is the baseline development fuel.

## 2. Coordinate system and envelope

- **Given:** millimetres, approximately 1000 mm overall length, approximately 400 mm maximum diameter, horizontal axis.
- **Derived:** +X is the engine axis from inlet to exhaust; +Y is lateral; +Z is up. The inlet tip is X=0 and nozzle exit is X=1000.
- **Derived:** maximum flange radius is 200 mm, so the casing envelope is exactly 400 mm diameter. Mount feet extend below the casing but remain inside the 400 mm radial envelope.
- **Assumed module stations:** inlet 0-140; compressor 140-490; diffuser/combustor 490-700; turbine 700-835; exhaust 835-1000 mm.
- **Packaging-only keep-outs:** shaft/rotor radial gap at least 0.20 mm at slip bores; blade-tip to casing gap at least 8 mm; axial rotor/stator clearance at least 10 mm; module faces are seated rather than visually floated. None is an approved running clearance.
- Primary datum A is the X axis; datum B is the inlet flange face; datum C is the vertical center plane.

## 3. Component architecture and BOM

The ICAD grammar has bodies, instances, joints, and manufacturing connections but no assembly-group declaration. Hierarchy is therefore encoded by stable body prefixes plus the module joint graph:

```text
ENGINE (WORLD / structure_compressor_casing)
├── INLET: inlet_cone, inlet_casing, inlet_struts, inlet_flange, inlet fasteners
├── COMPRESSOR
│   ├── ROTOR: six disks, six blade rows, spacers
│   ├── STATORS: six vane rows
│   └── CASING: compressor shell and bearing seat
├── COMBUSTOR: outer/inner perforated liners, injector tubes/mounts, diffuser and transition, casing
├── TURBINE
│   ├── ROTOR: one disk and one blade row
│   └── STATORS: one nozzle-guide-vane row
├── EXHAUST: tail cone, nozzle shell, flange and fasteners
├── BEARINGS: front and rear inner/outer races plus rolling elements
├── SHAFT: one continuous shaft with integral shoulders and retaining collars
└── STRUCTURE: five casing modules, flanges, two feet, access boss/cover, bolts
```

- **Assumed:** replaceable line items are bearings, fasteners, injectors, inspection cover, compressor/turbine blade bodies, and casing modules.
- **Assumed:** rotational repetition is represented as explicit equal-angle solid bodies because `CIRCULAR_PATTERN` is not advertised by the running compiler; each body remains independently inspectable.

## 4. Per-part construction

- Casing modules and nozzle: nickel alloy cast/turned blanks; analytic outer cylinder/cone minus coaxial inner cylinder/cone creates 5-10 mm walls; module faces and flange lands are machined.
- Shaft, disks, spacers, collars: nickel alloy bar/forgings, turned on datum A; disk bores and bearing journals finish-ground.
- Compressor blades and stators: titanium alloy mill/forge analogues; each solid unites a 10-point captured root with an eight-point cambered FREEFORM airfoil. Compressor rotors use nine interpolation sections and 26 degree twist; stators use seven sections and -18 degree twist. The front rotor is a 24-blade half-pitch pattern and downstream rows remain explicit 12-blade definitions.
- Turbine blades and NGVs: nickel superalloy multi-lobe root plus eight-point cambered FREEFORM airfoils with nine interpolation sections; hot-section dimensions and twist remain editable.
- Liners: 4 mm stainless sheet shells made by coaxial boolean subtraction, with genuine radial perforation bores.
- Injector tubes/mounts: stainless turned tubes and annular bosses; casing/liner clearance holes are real cuts.
- Bearings: steel turned races with separate rolling-element solids; bearing seats are turned annuli.
- Inspection cover: the mandatory CAD-history part uses a constrained multi-shape XY sketch, padded plate stock, and four true bolt-hole regions.
- Brackets: welded/machined structural-steel solids with mirrored left/right placement represented as distinct bodies; rounded/chamfered edges are applied where supported.
- **Compiler adaptation:** thin walls are modeled by exact coaxial booleans; repeated rows use explicit solids and assembly instances; loft/FREEFORM geometry remains analytic B-rep topology. The boolean kernel welds only sub-micron FREEFORM/root union fragments and records that repair provenance.

## 5. Interfaces and hardware

- Module flanges use paired `FLANGE` interfaces and `BOLTED` connections, ISO 4762 M6 hardware, eight holes on a 184 mm bolt-circle radius, 6.8 mm clearance bores, and outside tool access. Fastener strength, preload, thermal relaxation, locking, and flange-separation margins remain open evidence items.
- Shaft/bearings use paired `SHAFT`/`BEARING_SEAT` interfaces and ISO 492 bearing connections with 0.05 mm diametral running clearance in this benchmark.
- Rotor disks use 40.2 mm bores on a 40.0 mm shaft and are axially trapped by spacers/collars; torque keys/splines are outside the current compiler feature subset and are not claimed.
- Every physical blade/disk, disk/shaft, disk/spacer, bearing, support, and cone/shaft engagement is represented by an explicit ISO 286/492 manufacturing connection at a measured solid witness. The final review reports 262 declared engagements and zero unintended penetrations; root stress capability is not certified.
- Injector tubes pass through real clearance holes and seat at outer-liner bosses; assembly direction is radially inward.
- Inspection cover fasteners install vertically with unobstructed head access.

## 6. Kinematics

- `engine_ground` fixes the compressor casing to WORLD.
- Casing modules and structural feet are fixed children.
- `single_spool_rotation` is the only DOF: a revolute shaft joint about +X, home 0 degrees, inspection range -180 to +180 degrees.
- Rotor disks are fixed children of the shaft; bearings and stators remain fixed to casing structure.
- The inspection scene rotates only the shaft/rotor system through one revolution; casing does not move.

## 7. Tolerance strategy

- General modeled tolerance: 0.10 mm linear and 0.25 degree angular.
- Current packaging values: shaft-to-disk bore 0.10 mm radial; shaft-to-bearing inner race 0.025 mm radial; bearing outer race-to-seat 0.05 mm radial; compressor blade-tip gap 14-20 mm; turbine blade-tip gap 15 mm; rotor/stator axial gap at least 10 mm. These values must be replaced from tolerance, deformation, orbit, and thermal-growth evidence before Design Freeze R1.
- M6 flange holes are 6.8 mm diameter; bolt shanks are 6.0 mm; minimum flange radial edge distance is 12.6 mm.
- Liner wall is 4 mm; casing walls are 5-10 mm; blade/root minimum section is 3 mm; bracket/cover minimum thickness is 6 mm.
- Inspection dimensions: overall X length, maximum radial envelope, shaft diameter, each stage station, casing/liner wall, bearing-seat diameters, bolt-circle radius, and perforation diameter.

## 8. Visual intent

- Primary view is the clean longitudinal cutaway with +X horizontal, matching the supplied benchmark silhouette without joint-marker overlays.
- Required checks are front, right, and top lossless views plus a transverse/longitudinal section read-back.
- Module materials differentiate titanium compressor hardware, nickel hot-section hardware, stainless liners/injectors, bearing steel, and structural brackets without using textures as geometry.
- The assembly must visibly read as inlet cone -> six-stage compressor -> annular combustor -> single turbine -> convergent exhaust.

## 9. Deliverables and measurable acceptance

- Authoritative source: `turbojet_engine.icad`; this preparation record remains beside it.
- Required: clean check/diagnostics/validate/manufacturing; non-empty sketches and feature history; 1000 x <=400 x <=400 mm engine envelope excluding no hardware; six rotor rows and six stator rows; one turbine row and one NGV row; two bearing stations; genuine liner perforations and flange holes.
- Required visual acceptance: coherent inlet/end/side silhouette, 24-blade front rotor density, no rectangular inlet support spokes, visible six-stage axial progression, concentric shaft/casings, opaque perforated combustor liners inside translucent casing shells, and no joint-marker clutter in the clean cutaway.
- Required drawing acceptance: general arrangement is sheet 1; side elevation, inlet end view, longitudinal section A-A, centerlines, overall length and maximum diameter dimensions, ISO 129-1/5456-2/7200 release data, and family-level quantities for patterned blades and fasteners.
- Required assembly acceptance: named module/bearing connections report aligned and seated; shaft is the sole moving joint; unintended penetrating part pairs are zero or every physical engagement is explicitly explained and connected.
- Required build/read-back: STEP, STL, OBJ, glTF, GLB, 3MF, HTML, scene JSON, BOM, manufacturing report, SVG, DXF, and topology JSON; STEP/STL/GLB/3MF/DXF inspectors must succeed.

## Requirement traceability map

| Requirement | Planned ICAD entity | Check |
|---|---|---|
| ~1000 mm x ~400 mm, horizontal, mm | `engine_length`, `max_radius`, datum `engine_axis` | `measure`, visual bounds |
| Inlet cone/casing/flange/holes | `inlet_*` bodies and `inlet_flange_hole_*` cuts | topology and section |
| Six compressor stages | `compressor_rotor_disk_1..6`, blade/stator row bodies | name counts, right view |
| Shaft/disks/spacers/roots | `shaft_continuous`, rotor disks, spacer bodies, root+airfoil unions | section, body bounds |
| Annular combustor and perforations | `combustor_outer_liner`, `combustor_inner_liner`, radial hole cuts | topology, section |
| Injectors and transitions | `combustor_injector_*`, `diffuser_transition`, `turbine_transition` | right/top views |
| Turbine rotor/NGV/transition | `turbine_rotor_disk`, `turbine_blade_*`, `turbine_ngv_*` | counts, right view |
| Exhaust cone/nozzle/casing/flange | `exhaust_tail_cone`, `exhaust_nozzle`, `exhaust_flange` | bounds and section |
| Bearings/seats/retainers/clearances | `bearing_*`, `bearing_seat_*`, `shaft_retainer_*`, bearing interfaces | distance and connection JSON |
| Brackets/covers/holes/fasteners | `structure_mount_*`, `structure_inspection_cover`, M6 bodies | sketch history, top view |
| Analytic and advanced operations | `CYLINDER`, `CONE`, `LOFT`, `SWEEP`, `REVOLVE`, booleans, fillet/chamfer/mirror features | feature-history/topology JSON |
| Proper hierarchy and one rotating spool | prefixed bodies, fixed module joints, `single_spool_rotation` | joint graph and scene frames |
| Real solids/no floating surfaces | every body feature result; named interfaces/joints for module graph | manifold validation and interference |
| Complete manufacturing exports | build directory `build/icad/turbojet_engine` | format read-back inspectors |
