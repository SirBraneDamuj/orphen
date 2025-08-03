import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import os
import struct
import sys
import math

# --- Utility for 3D Math ---
def calculate_normal(v1, v2, v3):
    ux, uy, uz = v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2]
    vx, vy, vz = v3[0] - v1[0], v3[1] - v1[1], v3[2] - v1[2]
    nx, ny, nz = (uy * vz) - (uz * vy), (uz * vx) - (ux * vz), (ux * vy) - (uy * vx)
    length = math.sqrt(nx*nx + ny*ny + nz*nz)
    return (nx/length, ny/length, nz/length) if length > 0 else (0, 1, 0)

# --- Camera and Navigation ---
class Camera:
    def __init__(self):
        self.pos = [0, 0, 0]
        self.rotation = [45, 45]
        self.distance = 15000

    def frame_scene(self, bbox):
        min_coord, max_coord = bbox
        if any(c == float('inf') for c in min_coord) or any(c == float('-inf') for c in max_coord):
            return

        center_x = (min_coord[0] + max_coord[0]) / 2
        center_y = (min_coord[1] + max_coord[1]) / 2
        center_z = (min_coord[2] + max_coord[2]) / 2
        self.pos = [center_x, center_y, center_z]

        size_x = max_coord[0] - min_coord[0]
        size_y = max_coord[1] - min_coord[1]
        size_z = max_coord[2] - min_coord[2]
        max_dim = max(size_x, size_y, size_z)
        
        fov_rad = math.radians(45 / 2.0)
        distance = (max_dim / 2.0) / math.tan(fov_rad)
        self.distance = max(500, distance * 1.2)

    def update(self, events, keys):
        for event in events:
            if event.type == pygame.MOUSEMOTION and pygame.mouse.get_pressed()[0]:
                self.rotation[0] += event.rel[1]
                self.rotation[1] += event.rel[0]
            elif event.type == pygame.MOUSEWHEEL:
                self.distance = max(500, self.distance - event.y * 1000)
        
        speed = 100
        yaw_rad = math.radians(self.rotation[1])
        dx, dz = math.sin(yaw_rad) * speed, math.cos(yaw_rad) * speed
        
        if keys[K_w]:
            self.pos[0] -= dx
            self.pos[2] -= dz
        if keys[K_s]:
            self.pos[0] += dx
            self.pos[2] += dz
        if keys[K_a]:
            self.pos[0] -= dz
            self.pos[2] += dx
        if keys[K_d]:
            self.pos[0] += dz
            self.pos[2] -= dx
        if keys[K_q]: self.pos[1] -= speed
        if keys[K_e]: self.pos[1] += speed

    def apply(self):
        glLoadIdentity()
        glTranslatef(0, 0, -self.distance)
        glRotatef(self.rotation[0], 1, 0, 0)
        glRotatef(self.rotation[1], 0, 1, 0)
        glTranslatef(-self.pos[0], -self.pos[1], -self.pos[2])

# --- Map Parsing Logic ---
def parse_map_for_render(map_filepath):
    if not os.path.exists(map_filepath): return []
    with open(map_filepath, "rb") as f: map_data = f.read()

    header_size = 32
    header_fields = struct.unpack('<8I', map_data[:header_size])
    packed_field_1 = header_fields[1]
    model_chunk_count = (packed_field_1 & 0x000000FF)
    model_descriptors_offset = header_size # Let's assume descriptors start right after the header
    
    print("\n--- Parsing Model Chunks ---")
    model_chunks = []
    for i in range(model_chunk_count):
        descriptor_offset = model_descriptors_offset + (i * 16)
        if len(map_data) < descriptor_offset + 16: break
        
        desc_data = struct.unpack('<4I', map_data[descriptor_offset : descriptor_offset + 16])
        vertex_ptr, poly_ptr, packed_counts, chunk_type = desc_data
        
        vertex_count = (packed_counts & 0x000000FF)
        poly_count = (packed_counts & 0x0000FF00) >> 8

        print(f"\n[Chunk {i:02d}] Verts: {vertex_count}, Polys: {poly_count}, Type: 0x{chunk_type:x}, VtxPtr: 0x{vertex_ptr:x}, PolyPtr: 0x{poly_ptr:x}")

        if vertex_count == 0 or vertex_ptr >= len(map_data) or poly_ptr >= len(map_data):
            print("  -> Skipping: No vertices or invalid pointers.")
            continue

        vertices = []
        for v_idx in range(vertex_count):
            vert_offset = vertex_ptr + (v_idx * 6)
            if vert_offset + 6 > len(map_data): break
            v = struct.unpack('<3h', map_data[vert_offset : vert_offset + 6])
            vertices.append(v)
        
        indices = []
        current_strip = []
        
        # Heuristic: Read a bit beyond the expected polygon data to find all strips.
        # The poly_count seems to be an underestimate in some cases.
        read_limit = poly_ptr + (poly_count * 4) + 512 # Read up to 512 extra bytes
        
        p_idx = 0
        while poly_ptr + p_idx < read_limit and poly_ptr + p_idx < len(map_data):
            index_offset = poly_ptr + p_idx
            
            index = map_data[index_offset]
            if index == 0xFF:
                if len(current_strip) > 2:
                    indices.append(list(current_strip)) # Fix 2: Append a copy
                current_strip = []
            else:
                current_strip.append(index)
            p_idx += 1
        
        if len(current_strip) > 2:
            indices.append(list(current_strip)) # Fix 2: Append a copy

        print(f"  -> Found {len(indices)} triangle strips.")
        if vertices and indices:
            model_chunks.append({'vertices': vertices, 'indices': indices})

    return model_chunks

# --- Main Rendering Function ---
def main(map_file):
    pygame.init()
    display = (1280, 720)
    pygame.display.set_mode(display, DOUBLEBUF | OPENGL)
    pygame.display.set_caption(f"Orphen Map Viewer - {os.path.basename(map_file)}")

    glClearColor(0.39, 0.58, 0.93, 1.0) # Set a non-black background
    
    glEnable(GL_DEPTH_TEST)
    glDisable(GL_CULL_FACE) 
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL)

    # Setup projection matrix
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluPerspective(45, (display[0] / display[1]), 0.1, 200000.0)
    glMatrixMode(GL_MODELVIEW)

    print("Loading map...")
    models = parse_map_for_render(map_file)
    if not models:
        print("\nCould not find any renderable models in the map file.")
        return
    print(f"\nSuccessfully loaded {len(models)} model chunks.")

    camera = Camera()
    
    # --- Calculate Bounding Box and Frame Scene ---
    min_coord = [float('inf')] * 3
    max_coord = [float('-inf')] * 3
    for model in models:
        for v in model['vertices']:
            for i in range(3):
                min_coord[i] = min(min_coord[i], v[i])
                max_coord[i] = max(max_coord[i], v[i])
    camera.frame_scene((min_coord, max_coord))
    print(f"Model bounds: min={min_coord}, max={max_coord}")
    print(f"Camera positioned at: {camera.pos}, distance: {camera.distance}")
    # ---

    clock = pygame.time.Clock()

    while True:
        events = []
        for event in pygame.event.get():
            events.append(event)
            if event.type == pygame.QUIT or (event.type == KEYDOWN and event.key == K_ESCAPE):
                pygame.quit()
                return
        
        keys = pygame.key.get_pressed()
        camera.update(events, keys)

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        camera.apply()

        # Render models
        for model_idx, model in enumerate(models):
            vertices = model['vertices']
            if not vertices:
                continue

            for strip_idx, strip in enumerate(model['indices']):
                if not strip:
                    continue
                
                # Render as points
                glPointSize(5)
                glBegin(GL_POINTS)
                glColor3f(1.0, 1.0, 1.0) # White points
                for index in strip:
                    if index < len(vertices):
                        glVertex3fv(vertices[index])
                glEnd()

                # Render as lines
                glBegin(GL_LINE_STRIP)
                glColor3f(1.0, 0.0, 0.0) # Red lines
                for index in strip:
                    if index < len(vertices):
                        glVertex3fv(vertices[index])
                glEnd()

        pygame.display.flip()
        clock.tick(60)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <path_to_map_file>")
        sys.exit(1)
    main(sys.argv[1])
