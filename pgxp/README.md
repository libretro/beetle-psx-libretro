Description based on iCatButler's original post: http://ngemu.com/threads/pcsxr-pgxp.186369/

-------
## PGXP

PGXP (Parallel/Precision Geometry Transform Pipeline) is an enhancement for PlayStation emulation that produces high precision fully 3D geometry data that was not available on the original console hardware.

It is currently integrated with PCSX-Reloaded via Pete's OpenGL v1.78 plugin and Tapeq's Tweak extension to Pete's OpenGL v2.9 plugin, in addition to Beetle PSX

Note: This project is still very much a work in progress.

### Features
* High Precision Vertex Data (more stable geometry)
* Reduced Triangle Culling (more detailed models)
* Perspective Correct Texture Mapping (reduced texture distortion)

Chrono Cross: Distortion of model geometry is significantly reduced, especially at a distance.
<img src=http://i.imgur.com/EtPOZtG.png>

Ridge Racer Type 4: Higher precision culling calculations mean that small triangles are no longer culled
<img src=http://i.imgur.com/JbFjTxY.png>

Tomb Raider: 3D vertex coordinates mean affine texture mapping can be replaced by perspective correct mapping
<img src=http://i.imgur.com/vcd6eS2.png>

### Setup instructions
In Beetle PSX, ensure you are using either the 'opengl' or 'vulkan' renderers. Then change the PGXP-related core options to your liking.

**Thanks to:**

Tapeq

Pete Bernert

Edgbla

Simias

The PCSX-R team

And everyone who has provided feedback.
