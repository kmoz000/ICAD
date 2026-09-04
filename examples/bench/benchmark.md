# INDUSTRIAL CAD BENCHMARK — TURBOJET ENGINE

```
Create a complete manufacturable mechanical CAD assembly of a
small single-spool turbojet engine.

The model must represent a real engineered machine rather than
a visual 3D scene.

TARGET:

~1 meter overall length
~400 mm maximum casing diameter
horizontal engine axis
real-world millimeter units

ASSEMBLY:

1. FRONT INLET
   - inlet cone
   - inlet casing
   - mounting flange
   - fastener holes

2. COMPRESSOR
   - 6 axial compressor stages
   - rotor shaft
   - rotor disks
   - compressor blades
   - stator vanes
   - spacers
   - blade roots
   - casing
   - bearing interfaces

3. COMBUSTOR
   - annular combustion chamber
   - outer liner
   - inner liner
   - injector mounts
   - perforations
   - transition section
   - casing

4. TURBINE
   - turbine shaft
   - turbine disks
   - turbine blades
   - stator/nozzle guide vanes
   - spacers
   - blade roots
   - exhaust transition

5. EXHAUST
   - exhaust cone
   - nozzle
   - outer casing
   - mounting flange

6. ROTATING SYSTEM
   - continuous shaft
   - bearings
   - bearing seats
   - retaining features
   - realistic axial clearances

7. STRUCTURE
   - engine casing
   - mounting brackets
   - flanges
   - inspection covers
   - bolt holes
   - fasteners

GEOMETRY REQUIREMENTS:

Every component must be a genuine CAD solid.

Use:
- analytic cylinders
- cones
- planes
- revolutions
- sweeps
- lofts
- fillets
- chamfers
- booleans
- circular patterns
- mirror operations
- thin-wall/shell operations
- NURBS surfaces where required

Avoid:
- mesh approximations
- visual-only geometry
- disconnected surfaces
- fake textures representing geometry
- floating components

MANUFACTURING REQUIREMENTS:

Parts should be designed as real manufactured components.

Consider:
- machining
- casting
- sheet-metal fabrication
- turning
- milling
- realistic wall thickness
- realistic fastener placement
- shaft clearances
- bearing seats
- assembly access
- maintenance access

ASSEMBLY REQUIREMENTS:

Create a proper hierarchy:

ENGINE
├── INLET
├── COMPRESSOR
│   ├── ROTOR
│   ├── STATORS
│   └── CASING
├── COMBUSTOR
├── TURBINE
│   ├── ROTOR
│   └── STATORS
├── EXHAUST
├── BEARINGS
├── SHAFT
└── STRUCTURE
```
![alt text](image.png)