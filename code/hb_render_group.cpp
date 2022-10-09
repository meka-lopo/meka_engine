/*
 * Written by Gyuhyun Lee
 */

#include "hb_render_group.h"


struct vertex_normal_hit
{
    v3 normal_sum;
    i32 hit;
};

// TODO(gh): exclude duplicate vertex normals
internal void
generate_vertex_normals(MemoryArena *permanent_arena , MemoryArena *transient_arena, RawMesh *raw_mesh)
{
    assert(!raw_mesh->normals);

    raw_mesh->normal_count = raw_mesh->position_count;
    raw_mesh->normals = push_array(permanent_arena, v3, sizeof(v3)*raw_mesh->normal_count);

    TempMemory mesh_construction_temp_memory = start_temp_memory(transient_arena, megabytes(16));
    vertex_normal_hit *normal_hits = push_array(&mesh_construction_temp_memory, vertex_normal_hit, raw_mesh->position_count);

#if HB_DEBUG
    begin_cycle_counter(generate_vertex_normals);
#endif

    for(u32 normal_index = 0;
            normal_index < raw_mesh->normal_count;
            ++normal_index)
    {
        vertex_normal_hit *normal_hit = normal_hits + normal_index;
        *normal_hit = {};
    }

    for(u32 i = 0;
            i < raw_mesh->index_count;
            i += 3)
    {
        u32 i0 = raw_mesh->indices[i];
        u32 i1 = raw_mesh->indices[i+1];
        u32 i2 = raw_mesh->indices[i+2];

        v3 v0 = raw_mesh->positions[i0] - raw_mesh->positions[i1];
        v3 v1 = raw_mesh->positions[i2] - raw_mesh->positions[i1];

        v3 normal = cross(v1, v0);
        normal_hits[i0].normal_sum += normal;
        normal_hits[i0].hit++;
        normal_hits[i1].normal_sum += normal;
        normal_hits[i1].hit++;
        normal_hits[i2].normal_sum += normal;
        normal_hits[i2].hit++;
    }
 
    for(u32 normal_index = 0;
            normal_index < raw_mesh->normal_count;
            ++normal_index)
    {
        vertex_normal_hit *normal_hit = normal_hits + normal_index;
        assert(normal_hit->hit);

        v3 result_normal = normalize(normal_hit->normal_sum/(r32)normal_hit->hit);

        raw_mesh->normals[normal_index] = result_normal;
    }

#if HB_DEBUG
    end_cycle_counter(generate_vertex_normals);
#endif

    end_temp_memory(&mesh_construction_temp_memory);
}

/*TODO(gh): 
    This routine is slightly optimized using 128bit-wide SIMD vectors, with around ~2x improvements than before
    Improvements can be made with:
    1. better loading by organizing the data more simd-friendly
    2. using larger simd vectors (float32x4x4?)

    Also, this _only_ works for 4 wide lane simd, and does not work for 1(due to how the memory laid out)
*/
internal void
 generate_vertex_normals_simd(MemoryArena *permanent_arena, MemoryArena *trasient_arena, RawMesh *raw_mesh)
 {
     assert(!raw_mesh->normals);

     raw_mesh->normal_count = raw_mesh->position_count;
     raw_mesh->normals = push_array(permanent_arena, v3, sizeof(v3)*raw_mesh->normal_count);

     TempMemory mesh_construction_temp_memory = start_temp_memory(trasient_arena, megabytes(16), true);

     r32 *normal_sum_x = push_array(&mesh_construction_temp_memory, r32, raw_mesh->position_count);
     r32 *normal_sum_y = push_array(&mesh_construction_temp_memory, r32, raw_mesh->position_count);
     r32 *normal_sum_z = push_array(&mesh_construction_temp_memory, r32, raw_mesh->position_count);

     u32 *normal_hit_counts = push_array(&mesh_construction_temp_memory, u32, raw_mesh->position_count);

     // NOTE(gh): dividing by 12 as each face is consisted of 3 indices, and we are going to use 4-wide(128 bits) SIMD
     //u32 index_count_div_12 = face_count / 12;
     u32 index_count_mod_12 = raw_mesh->index_count % 12;
     u32 simd_end_index = raw_mesh->index_count - index_count_mod_12;

 #define u32_from_slot(vector, slot) (((u32 *)&vector)[slot])
 #define r32_from_slot(vector, slot) (((r32 *)&vector)[slot])

 #if HB_DEBUG
     begin_cycle_counter(generate_vertex_normals);
 #endif
     // TODO(gh) : If we decide to lose the precision while calculating the normals,
     // might be able to use float16, which doubles the faces that we can process at once
     for(u32 i = 0;
             i < simd_end_index;
             i += 12) // considering we are using 128 bit wide vectors
     {
         // Load 12 indices
         // TODO(gh): timed this and for now it's not worth doing so(and increase the code complexity), 
         // unless we change the index to be 16-bit?
         // i00, i01 i02 i10
         uint32x4_t ia = vld1q_u32(raw_mesh->indices + i);
         // i11, i12 i20 i21
         uint32x4_t ib = vld1q_u32(raw_mesh->indices + i + 4);
         // i22, i30 i31 i32
         uint32x4_t ic = vld1q_u32(raw_mesh->indices + i + 8);

         v3 v00 = raw_mesh->positions[u32_from_slot(ia, 0)]; 
         v3 v01 = raw_mesh->positions[u32_from_slot(ia, 1)]; 
         v3 v02 = raw_mesh->positions[u32_from_slot(ia, 2)];

         v3 v10 = raw_mesh->positions[u32_from_slot(ia, 3)]; 
         v3 v11 = raw_mesh->positions[u32_from_slot(ib, 0)]; 
         v3 v12 = raw_mesh->positions[u32_from_slot(ib, 1)];

         v3 v20 = raw_mesh->positions[u32_from_slot(ib, 2)]; 
         v3 v21 = raw_mesh->positions[u32_from_slot(ib, 3)]; 
         v3 v22 = raw_mesh->positions[u32_from_slot(ic, 0)];

         v3 v30 = raw_mesh->positions[u32_from_slot(ic, 1)]; 
         v3 v31 = raw_mesh->positions[u32_from_slot(ic, 2)]; 
         v3 v32 = raw_mesh->positions[u32_from_slot(ic, 3)];

         float32x4_t v0_x = {v00.x, v10.x, v20.x, v30.x};
         float32x4_t v0_y = {v00.y, v10.y, v20.y, v30.y};
         float32x4_t v0_z = {v00.z, v10.z, v20.z, v30.z};

         float32x4_t v1_x = {v01.x, v11.x, v21.x, v31.x};
         float32x4_t v1_y = {v01.y, v11.y, v21.y, v31.y};
         float32x4_t v1_z = {v01.z, v11.z, v21.z, v31.z};

         float32x4_t v2_x = {v02.x, v12.x, v22.x, v32.x};
         float32x4_t v2_y = {v02.y, v12.y, v22.y, v32.y};
         float32x4_t v2_z = {v02.z, v12.z, v22.z, v32.z};

         // NOTE(gh): e0 = v1 - v0, e1 = v2 - v0, normal = cross(e0, e1);

         float32x4_t e0_x_4 = vsubq_f32(v1_x, v0_x);
         float32x4_t e0_y_4 = vsubq_f32(v1_y, v0_y);
         float32x4_t e0_z_4 = vsubq_f32(v1_z, v0_z);

         float32x4_t e1_x_4 = vsubq_f32(v2_x, v0_x);
         float32x4_t e1_y_4 = vsubq_f32(v2_y, v0_y);
         float32x4_t e1_z_4 = vsubq_f32(v2_z, v0_z);

         // x,     y,     z 
         // y0*z1, z0*x1, x0*y1
         float32x4_t cross_left_x = vmulq_f32(e0_y_4, e1_z_4);
         float32x4_t cross_left_y = vmulq_f32(e0_z_4, e1_x_4);
         float32x4_t cross_left_z = vmulq_f32(e0_x_4, e1_y_4);

         // x,     y,     z 
         // z0*y1, x0*z1, y0*x1
         float32x4_t cross_right_x = vmulq_f32(e1_y_4, e0_z_4);
         float32x4_t cross_right_y = vmulq_f32(e0_x_4, e1_z_4);
         float32x4_t cross_right_z = vmulq_f32(e0_y_4, e1_x_4);

         float32x4_t cross_result_x = vsubq_f32(cross_left_x, cross_right_x);
         float32x4_t cross_result_y = vsubq_f32(cross_left_y, cross_right_y);
         float32x4_t cross_result_z = vsubq_f32(cross_left_z, cross_right_z);

         // NOTE(gh): now we have a data that looks like : xxxx yyyy zzzz

         // ia : i00, i01 i02 i10
         // ib : i11, i12 i20 i21
         // ic : i22, i30 i31 i32

         normal_hit_counts[u32_from_slot(ia, 0)]++;
         normal_hit_counts[u32_from_slot(ia, 1)]++;
         normal_hit_counts[u32_from_slot(ia, 2)]++;

         normal_hit_counts[u32_from_slot(ia, 3)]++;
         normal_hit_counts[u32_from_slot(ib, 0)]++;
         normal_hit_counts[u32_from_slot(ib, 1)]++;

         normal_hit_counts[u32_from_slot(ib, 2)]++;
         normal_hit_counts[u32_from_slot(ib, 3)]++;
         normal_hit_counts[u32_from_slot(ic, 0)]++;

         normal_hit_counts[u32_from_slot(ic, 1)]++;
         normal_hit_counts[u32_from_slot(ic, 2)]++;
         normal_hit_counts[u32_from_slot(ic, 3)]++;

         normal_sum_x[u32_from_slot(ia, 0)] += r32_from_slot(cross_result_x, 0);
         normal_sum_x[u32_from_slot(ia, 1)] += r32_from_slot(cross_result_x, 0);
         normal_sum_x[u32_from_slot(ia, 2)] += r32_from_slot(cross_result_x, 0);
         normal_sum_y[u32_from_slot(ia, 0)] += r32_from_slot(cross_result_y, 0);
         normal_sum_y[u32_from_slot(ia, 1)] += r32_from_slot(cross_result_y, 0);
         normal_sum_y[u32_from_slot(ia, 2)] += r32_from_slot(cross_result_y, 0);
         normal_sum_z[u32_from_slot(ia, 0)] += r32_from_slot(cross_result_z, 0);
         normal_sum_z[u32_from_slot(ia, 1)] += r32_from_slot(cross_result_z, 0);
         normal_sum_z[u32_from_slot(ia, 2)] += r32_from_slot(cross_result_z, 0);

         normal_sum_x[u32_from_slot(ia, 3)] += r32_from_slot(cross_result_x, 1);
         normal_sum_x[u32_from_slot(ib, 0)] += r32_from_slot(cross_result_x, 1);
         normal_sum_x[u32_from_slot(ib, 1)] += r32_from_slot(cross_result_x, 1);
         normal_sum_y[u32_from_slot(ia, 3)] += r32_from_slot(cross_result_y, 1);
         normal_sum_y[u32_from_slot(ib, 0)] += r32_from_slot(cross_result_y, 1);
         normal_sum_y[u32_from_slot(ib, 1)] += r32_from_slot(cross_result_y, 1);
         normal_sum_z[u32_from_slot(ia, 3)] += r32_from_slot(cross_result_z, 1);
         normal_sum_z[u32_from_slot(ib, 0)] += r32_from_slot(cross_result_z, 1);
         normal_sum_z[u32_from_slot(ib, 1)] += r32_from_slot(cross_result_z, 1);

         normal_sum_x[u32_from_slot(ib, 2)] += r32_from_slot(cross_result_x, 2);
         normal_sum_x[u32_from_slot(ib, 3)] += r32_from_slot(cross_result_x, 2);
         normal_sum_x[u32_from_slot(ic, 0)] += r32_from_slot(cross_result_x, 2);
         normal_sum_y[u32_from_slot(ib, 2)] += r32_from_slot(cross_result_y, 2);
         normal_sum_y[u32_from_slot(ib, 3)] += r32_from_slot(cross_result_y, 2);
         normal_sum_y[u32_from_slot(ic, 0)] += r32_from_slot(cross_result_y, 2);
         normal_sum_z[u32_from_slot(ib, 2)] += r32_from_slot(cross_result_z, 2);
         normal_sum_z[u32_from_slot(ib, 3)] += r32_from_slot(cross_result_z, 2);
         normal_sum_z[u32_from_slot(ic, 0)] += r32_from_slot(cross_result_z, 2);

         normal_sum_x[u32_from_slot(ic, 1)] += r32_from_slot(cross_result_x, 3);
         normal_sum_x[u32_from_slot(ic, 2)] += r32_from_slot(cross_result_x, 3);
         normal_sum_x[u32_from_slot(ic, 3)] += r32_from_slot(cross_result_x, 3);
         normal_sum_y[u32_from_slot(ic, 1)] += r32_from_slot(cross_result_y, 3);
         normal_sum_y[u32_from_slot(ic, 2)] += r32_from_slot(cross_result_y, 3);
         normal_sum_y[u32_from_slot(ic, 3)] += r32_from_slot(cross_result_y, 3);
         normal_sum_z[u32_from_slot(ic, 1)] += r32_from_slot(cross_result_z, 3);
         normal_sum_z[u32_from_slot(ic, 2)] += r32_from_slot(cross_result_z, 3);
         normal_sum_z[u32_from_slot(ic, 3)] += r32_from_slot(cross_result_z, 3);

     }

     // NOTE(gh): Get the rest of the normals, that cannot be processed in vector-wide fassion
     for(u32 i = simd_end_index;
             i < raw_mesh->index_count;
             i += 3)
     {
         u32 i0 = raw_mesh->indices[i];
         u32 i1 = raw_mesh->indices[i+1];
         u32 i2 = raw_mesh->indices[i+2];

         v3 v0 = raw_mesh->positions[i0] - raw_mesh->positions[i1];
         v3 v1 = raw_mesh->positions[i2] - raw_mesh->positions[i1];

         v3 normal = cross(v1, v0);

         normal_sum_x[i0] += normal.x;
         normal_sum_y[i0] += normal.y;
         normal_sum_z[i0] += normal.z;

         normal_sum_x[i1] += normal.x;
         normal_sum_y[i1] += normal.y;
         normal_sum_z[i1] += normal.z;

         normal_sum_x[i2] += normal.x;
         normal_sum_y[i2] += normal.y;
         normal_sum_z[i2] += normal.z;

         normal_hit_counts[i0]++;
         normal_hit_counts[i1]++;
         normal_hit_counts[i2]++;
     }


     u32 normal_count_mod_4 = raw_mesh->normal_count % 4;
     u32 simd_normal_end_index = raw_mesh->normal_count - normal_count_mod_4;

     for(u32 i = 0;
             i  < simd_normal_end_index;
             i += 4)
     {
         //assert(normal_hit_counts[normal_index]);

         float32x4_t normal_sum_x_128 = vld1q_f32(normal_sum_x + i);
         float32x4_t normal_sum_y_128 = vld1q_f32(normal_sum_y + i);
         float32x4_t normal_sum_z_128 = vld1q_f32(normal_sum_z + i);
         // TODO(gh): test convert vs reinterpret
         float32x4_t normal_hit_counts_128 = vcvtq_f32_u32(vld1q_u32(normal_hit_counts + i));

         // TODO(gh): div vs load one over
         float32x4_t unnormalized_result_x_128 = vdivq_f32(normal_sum_x_128, normal_hit_counts_128);
         float32x4_t unnormalized_result_y_128 = vdivq_f32(normal_sum_y_128, normal_hit_counts_128);
         float32x4_t unnormalized_result_z_128 = vdivq_f32(normal_sum_z_128, normal_hit_counts_128);

         // TODO(gh): normalizing the simd vector might come in handy, pull this out into a seperate function?
         float32x4_t dot_128 = vaddq_f32(vaddq_f32(vmulq_f32(unnormalized_result_x_128, unnormalized_result_x_128), vmulq_f32(unnormalized_result_y_128, unnormalized_result_y_128)), 
                                         vmulq_f32(unnormalized_result_z_128, unnormalized_result_z_128));
         float32x4_t length_128 = vsqrtq_f32(dot_128);

         float32x4_t normalized_result_x_128 = vdivq_f32(unnormalized_result_x_128, length_128);
         float32x4_t normalized_result_y_128 = vdivq_f32(unnormalized_result_y_128, length_128);
         float32x4_t normalized_result_z_128 = vdivq_f32(unnormalized_result_z_128, length_128);

         raw_mesh->normals[i+0] = V3(r32_from_slot(normalized_result_x_128, 0), 
                                     r32_from_slot(normalized_result_y_128, 0), 
                                     r32_from_slot(normalized_result_z_128, 0));
         raw_mesh->normals[i+1] = V3(r32_from_slot(normalized_result_x_128, 1), 
                                     r32_from_slot(normalized_result_y_128, 1), 
                                     r32_from_slot(normalized_result_z_128, 1));
         raw_mesh->normals[i+2] = V3(r32_from_slot(normalized_result_x_128, 2), 
                                     r32_from_slot(normalized_result_y_128, 2), 
                                     r32_from_slot(normalized_result_z_128, 2));
         raw_mesh->normals[i+3] = V3(r32_from_slot(normalized_result_x_128, 3), 
                                     r32_from_slot(normalized_result_y_128, 3), 
                                     r32_from_slot(normalized_result_z_128, 3));
     }

     // NOTE(gh): calculate the remaining normals
     for(u32 normal_index = simd_normal_end_index;
             normal_index < raw_mesh->normal_count;
             normal_index++)
     {
         v3 normal_sum = V3(normal_sum_x[normal_index], normal_sum_y[normal_index], normal_sum_z[normal_index]);
         u32 normal_hit_count = normal_hit_counts[normal_index];
         assert(normal_hit_count);

         v3 result_normal = normalize(normal_sum/(r32)normal_hit_count);

         raw_mesh->normals[normal_index] = result_normal;
     }

#if HB_DEBUG
     end_cycle_counter(generate_vertex_normals);
#endif

     end_temp_memory(&mesh_construction_temp_memory);
}

internal Camera
init_fps_camera(v3 p, f32 focal_length, f32 fov_in_degree, f32 near, f32 far)
{
    Camera result = {};

    result.p = p;
    result.focal_length = focal_length;

    result.fov = degree_to_radian(fov_in_degree);
    result.near = near;
    result.far = far;

    result.pitch = 0.0f;
    result.yaw = 0.0f;
    result.roll = 0.0f;

    return result;
}

internal CircleCamera
init_circle_camera(v3 p, v3 lookat_p, f32 distance_from_axis, f32 fov_in_degree, f32 near, f32 far)
{
    CircleCamera result = {};

    result.p = p;
    // TODO(gh) we are not allowing arbitrary point for this type of camera, for now.
    assert(p.x == 0.0f && p.y == 0);
    result.lookat_p = lookat_p;
    result.distance_from_axis = distance_from_axis;
    result.rad = 0;

    result.fov = degree_to_radian(fov_in_degree);
    result.near = near;
    result.far = far;

    return result;
}

/* 
   NOTE(gh) Rotation matrix
   Let's say that we have a tranform matrix that looks like this
   |xa ya za|
   |xb yb zb|
   |xc yc zc|

   This means that whenever we multiply a point with this, we are projecting the point into a coordinate system
   with three axes : [xa ya za], [xb yb zb], [xc yc zc] by taking a dot product.
   Common example for this would be world to camera transform matrix.

   Let's now setup the matrix in the other way by transposing the matrix above. 
   |xa xb xc|
   |ya yb yc|
   |za zb zc|
   Whenever we multiply a point by this, the result would be p.x*[xa ya za] + p.y*[xb yb zb] + p.z*[xc yc zc]
   Common example for this would be camera to world matrix, where we have a point in camera space and 
   we want to get what the p would be in world coordinates 

   As you can see, two matrix is doing the exact opposite thing, which means they are inverse matrix to each other.
   This make sense, as the rotation matrix is an orthogonal matrix(both rows and the columns are unit vectors),
   so that the inverse = transpose
*/

// NOTE(gh) To move the world space position into camera space,
// 1. We first translate the position so that the origin of the camera space
// in world coordinate is (0, 0, 0). We can achieve this by  simply subtracing the camera position
// from the world position.
// 2. We then project the translated position into the 3 camera axes. This is done simply by using a dot product.
//
// To pack this into a single matrix, we need to multiply the translation by the camera axis matrix,
// because otherwise it will be projection first and then translation -> which is completely different from
// doing the translation and the projection.
internal m4x4 
camera_transform(v3 camera_p, v3 camera_x_axis, v3 camera_y_axis, v3 camera_z_axis)
{
    m4x4 result = {};

    // NOTE(gh) to pack the rotation & translation into one matrix(with an order of translation and the rotation),
    // we need to first multiply the translation by the rotation matrix
    v3 multiplied_translation = V3(dot(camera_x_axis, -camera_p), 
                                    dot(camera_y_axis, -camera_p),
                                    dot(camera_z_axis, -camera_p));

    result.rows[0] = V4(camera_x_axis, multiplied_translation.x);
    result.rows[1] = V4(camera_y_axis, multiplied_translation.y);
    result.rows[2] = V4(camera_z_axis, multiplied_translation.z);
    result.rows[3] = V4(0.0f, 0.0f, 0.0f, 1.0f); // Dont need to touch the w part, view matrix doesn't produce homogeneous coordinates

    return result;
}

internal m4x4
camera_transform(Camera *camera)
{
    // NOTE(gh) FPS camear removes one axis to avoid gimbal lock. 
    m3x3 camera_local_rotation = z_rotate(camera->roll) * x_rotate(camera->pitch);
    
    // NOTE(gh) camera aligns with the world coordinate in default.
    v3 camera_x_axis = normalize(camera_local_rotation * V3(1, 0, 0));
    v3 camera_y_axis = normalize(camera_local_rotation * V3(0, 1, 0));
    v3 camera_z_axis = normalize(camera_local_rotation * V3(0, 0, 1));
    m3x3 transpose_camera_local_rotation = transpose(camera_local_rotation);

    return camera_transform(camera->p, camera_x_axis, camera_y_axis, camera_z_axis);
}

internal m4x4
camera_transform(CircleCamera *camera)
{
    // -z is the looking direction
    v3 camera_z_axis = -normalize(camera->lookat_p - camera->p);

    // TODO(gh) This does not work if the camera was direction looking down or up in world z axis
    assert(!(camera_z_axis.x == 0.0f && camera_z_axis.y == 0.0f));
    v3 camera_x_axis = normalize(cross(V3(0, 0, 1), camera_z_axis));
    v3 camera_y_axis = normalize(cross(camera_z_axis, camera_x_axis));

    return camera_transform(camera->p, camera_x_axis, camera_y_axis, camera_z_axis);
}

// NOTE(gh) persepctive projection matrix for (-1, -1, 0) to (1, 1, 1) NDC like Metal
/*
    Little tip in how we get the persepctive projection matrix
    Think as 2D plane(x and z OR y and z), use triangle similarity to get projected Xp and Yp
    For Z, as x and y don't have any effect on Z, we can say (A * Ze + b) / -Ze = Zp (diving by -Ze because homogeneous coords)

    -n should produce 0 or -1 value(based on what NDC system we use), 
    while -f should produce 1.
*/
inline m4x4
perspective_projection_near_is_01(f32 fov, f32 n, f32 f, f32 width_over_height)
{
    assert(fov < 180);

    f32 half_near_plane_width = n*tanf(0.5f*fov)*0.5f;
    f32 half_near_plane_height = half_near_plane_width / width_over_height;

    m4x4 result = {};

    // TODO(gh) Even though the resulting coordinates should be same, 
    // it seems like in Metal w value should be positive.
    // Maybe that's how they are doing the frustum culling..?
    result.rows[0] = V4(n / half_near_plane_width, 0, 0, 0);
    result.rows[1] = V4(0, n / half_near_plane_height, 0, 0);
    result.rows[2] = V4(0, 0, f/(n-f), (n*f)/(n-f)); // X and Y values don't effect the z value
    result.rows[3] = V4(0, 0, -1, 0); // As Xp and Yp are dependant to Z value, this is the only way to divide Xp and Yp by -Ze

    return result;
}


internal v3
get_camera_lookat(Camera *camera)
{
    // TODO(gh) I don't think the math here is correct, shouldn't we do this in backwards 
    // to go from camera space to world space, considering that (0, 0, -1) is the camera lookat in
    // camera space??
    m3x3 camera_local_rotation = z_rotate(camera->roll) * x_rotate(camera->pitch);
    v3 result = camera_local_rotation * V3(0, 0, -1); 

    return result;
}

// TODO(gh) Does not work when camera is directly looking up or down
internal v3
get_camera_right(Camera *camera)
{
    // TODO(gh) Up vector might be same as the camera direction
    v3 camera_dir = get_camera_lookat(camera);
    v3 result = normalize(cross(camera_dir, V3(0, 0, 1)));

    return result;
}

// TODO(gh) This operation is quite expensive, find out to optimize it
internal void
get_camera_frustum(Camera *camera, CameraFrustum *frustum, f32 width_over_height)
{
    v3 camera_dir = get_camera_lookat(camera);
    v3 camera_right = get_camera_right(camera);
    v3 camera_up = normalize(cross(camera_right, camera_dir));

    v3 near_plane_center = camera->p + camera->near * camera_dir;
    v3 far_plane_center = camera->p + camera->far * camera_dir;

    f32 half_near_plane_width = camera->near*tanf(0.5f*camera->fov)*0.5f;
    f32 half_near_plane_height = half_near_plane_width / width_over_height;
    v3 half_near_plane_right = half_near_plane_width * camera_right;
    v3 half_near_plane_up = half_near_plane_height * camera_up;

    f32 half_far_plane_width = camera->far*tanf(0.5f*camera->fov)*0.5f;
    f32 half_far_plane_height = half_far_plane_width / width_over_height;
    v3 half_far_plane_right = half_far_plane_width * camera_right;
    v3 half_far_plane_up = half_far_plane_height * camera_up;

    // morten z order
    frustum->near[0] = near_plane_center - half_near_plane_right + half_near_plane_up;
    frustum->near[1] = near_plane_center + half_near_plane_right + half_near_plane_up;
    frustum->near[2] = near_plane_center - half_near_plane_right - half_near_plane_up;
    frustum->near[3] = near_plane_center + half_near_plane_right - half_near_plane_up;

    frustum->far[0] = far_plane_center - half_far_plane_right + half_far_plane_up;
    frustum->far[1] = far_plane_center + half_far_plane_right + half_far_plane_up;
    frustum->far[2] = far_plane_center - half_far_plane_right - half_far_plane_up;
    frustum->far[3] = far_plane_center + half_far_plane_right - half_far_plane_up;
}

internal m4x4
rhs_to_lhs(m4x4 m)
{
    m4x4 result = m;
    result.rows[2] *= -1.0f;

    return result;
}

// TODO(gh) Later we would want to minimize passing the platform buffer here
// TODO(gh) Make the grass count more configurable?
internal void
init_grass_grid(PlatformRenderPushBuffer *render_push_buffer, Entity *floor, RandomSeries *series, GrassGrid *grass_grid, u32 grass_count_x, u32 grass_count_y, v2 min, v2 max)
{
    grass_grid->grass_count_x = grass_count_x;
    grass_grid->grass_count_y = grass_count_y;
    grass_grid->updated_hash_buffer = false;
    grass_grid->updated_floor_z_buffer = false;
    grass_grid->min = min;
    grass_grid->max = max;

    u32 total_grass_count = grass_grid->grass_count_x * grass_grid->grass_count_y;

    if(!grass_grid->hash_buffer)
    {
        grass_grid->hash_buffer = (u32 *)((u8 *)render_push_buffer->giant_buffer + render_push_buffer->giant_buffer_used);
        grass_grid->hash_buffer_size = sizeof(u32) * total_grass_count;
        grass_grid->hash_buffer_offset = render_push_buffer->giant_buffer_used;

        for(u32 i = 0;
                i < total_grass_count;
                ++i)
        {
            grass_grid->hash_buffer[i] = random_between_u32(series, 0, 10000);
        }

        render_push_buffer->giant_buffer_used += grass_grid->hash_buffer_size;
        assert(render_push_buffer->giant_buffer_used <= render_push_buffer->giant_buffer_size);

        grass_grid->updated_hash_buffer = true;
    }

    if(!grass_grid->floor_z_buffer)
    {
        grass_grid->floor_z_buffer = (f32 *)((u8 *)render_push_buffer->giant_buffer + render_push_buffer->giant_buffer_used);
        grass_grid->floor_z_buffer_size = sizeof(f32) * total_grass_count;
        grass_grid->floor_z_buffer_offset = render_push_buffer->giant_buffer_used;

        render_push_buffer->giant_buffer_used += grass_grid->floor_z_buffer_size;
        assert(render_push_buffer->giant_buffer_used <= render_push_buffer->giant_buffer_size);

        raycast_to_populate_floor_z_buffer(floor, grass_grid->floor_z_buffer);

        grass_grid->updated_floor_z_buffer = true;
    }

    if(!grass_grid->perlin_noise_buffer)
    {
        grass_grid->perlin_noise_buffer = (f32 *)((u8 *)render_push_buffer->giant_buffer + render_push_buffer->giant_buffer_used);
        grass_grid->perlin_noise_buffer_size = sizeof(f32) * total_grass_count;
        grass_grid->perlin_noise_buffer_offset = render_push_buffer->giant_buffer_used;

        render_push_buffer->giant_buffer_used += grass_grid->perlin_noise_buffer_size;
        assert(render_push_buffer->giant_buffer_used <= render_push_buffer->giant_buffer_size);
    }
}

internal void
init_render_push_buffer(PlatformRenderPushBuffer *render_push_buffer, Camera *main_camera, Camera *game_camera,  
                        GrassGrid *grass_grids, u32 grass_grid_count_x, u32 grass_grid_count_y,
                        v3 clear_color, b32 enable_shadow)
{
    assert(render_push_buffer->base);

    render_push_buffer->main_camera_view = camera_transform(main_camera);
    render_push_buffer->main_camera_near = main_camera->near;
    render_push_buffer->main_camera_far = main_camera->far;
    render_push_buffer->main_camera_fov = main_camera->fov;
    render_push_buffer->main_camera_p = main_camera->p;

    render_push_buffer->game_camera_view = camera_transform(game_camera);
    render_push_buffer->game_camera_near = game_camera->near;
    render_push_buffer->game_camera_far = game_camera->far;
    render_push_buffer->game_camera_fov = game_camera->fov;
    render_push_buffer->game_camera_p = game_camera->p;

    render_push_buffer->clear_color = clear_color;
    render_push_buffer->grass_grids = grass_grids;
    render_push_buffer->grass_grid_count_x = grass_grid_count_x;
    render_push_buffer->grass_grid_count_y = grass_grid_count_y;

    render_push_buffer->enable_shadow = enable_shadow;
    
    render_push_buffer->combined_vertex_buffer_used = 0;
    render_push_buffer->combined_index_buffer_used = 0;

    render_push_buffer->used = 0;
}

// TODO(gh) do we want to collape this to single line_group or something to save memory(color, type)?
internal void
push_line(PlatformRenderPushBuffer *render_push_buffer, v3 start, v3 end, v3 color)
{
    RenderEntryLine *entry = (RenderEntryLine *)(render_push_buffer->base + render_push_buffer->used);
    render_push_buffer->used += sizeof(*entry);
    entry->header.type = RenderEntryType_Line;

    entry->start = start;
    entry->end = end;
    entry->color = color;
}

// TODO(gh) not sure how this will hold up when the scene gets complicated(with a lot or vertices)
// so keep an eye on this method
internal u32
push_vertex_data(PlatformRenderPushBuffer *render_push_buffer, CommonVertex *vertices, u32 vertex_count)
{
    u32 result = render_push_buffer->combined_vertex_buffer_used;

    u32 size_to_copy = sizeof(vertices[0]) * vertex_count;

    void *dst = (u8 *)render_push_buffer->combined_vertex_buffer + render_push_buffer->combined_vertex_buffer_used;
    memcpy(dst, vertices, size_to_copy);

    render_push_buffer->combined_vertex_buffer_used += size_to_copy;
    assert(render_push_buffer->combined_vertex_buffer_used <= render_push_buffer->combined_vertex_buffer_size);

    // returns original used vertex buffer
    return result;
}

internal u32
push_index_data(PlatformRenderPushBuffer *render_push_buffer, u32 *indices, u32 index_count)
{
    u32 result = render_push_buffer->combined_index_buffer_used;

    u32 size_to_copy = sizeof(indices[0]) * index_count;
    
    void *dst = (u8 *)render_push_buffer->combined_index_buffer + render_push_buffer->combined_index_buffer_used;
    memcpy(dst, indices, size_to_copy);

    render_push_buffer->combined_index_buffer_used += size_to_copy;
    assert(render_push_buffer->combined_index_buffer_used <= render_push_buffer->combined_index_buffer_size);

    // returns original used index buffer
    return result;
}
 
internal void
push_aabb(PlatformRenderPushBuffer *render_push_buffer, v3 p, v3 dim, v3 color, 
          CommonVertex *vertices, u32 vertex_count, u32 *indices, u32 index_count, b32 should_cast_shadow)
{
    RenderEntryAABB *entry = (RenderEntryAABB *)(render_push_buffer->base + render_push_buffer->used);
    render_push_buffer->used += sizeof(*entry);
    entry->header.type = RenderEntryType_AABB;

    entry->p = p;
    entry->dim = dim;

    entry->should_cast_shadow = should_cast_shadow;
    
    entry->color = color;

    entry->vertex_buffer_offset = push_vertex_data(render_push_buffer, vertices, vertex_count);
    entry->index_buffer_offset = push_index_data(render_push_buffer, indices, index_count);
    entry->index_count = index_count;

    assert(render_push_buffer->used <= render_push_buffer->total_size);
}

internal void
push_cube(PlatformRenderPushBuffer *render_push_buffer, v3 p, v3 dim, v3 color, 
          CommonVertex *vertices, u32 vertex_count, u32 *indices, u32 index_count, b32 should_cast_shadow)
{
    RenderEntryCube *entry = (RenderEntryCube *)(render_push_buffer->base + render_push_buffer->used);
    render_push_buffer->used += sizeof(*entry);
    entry->header.type = RenderEntryType_Cube;

    assert(render_push_buffer->used <= render_push_buffer->total_size);

    entry->p = p;
    entry->dim = dim;

    entry->should_cast_shadow = should_cast_shadow;
    
    entry->color = color;

    entry->vertex_buffer_offset = push_vertex_data(render_push_buffer, vertices, vertex_count);
    entry->index_buffer_offset = push_index_data(render_push_buffer, indices, index_count);
    entry->index_count = index_count;
}

internal void
push_grass(PlatformRenderPushBuffer *render_push_buffer, v3 p, v3 dim, v3 color, 
          CommonVertex *vertices, u32 vertex_count, u32 *indices, u32 index_count, b32 should_cast_shadow)
{
    RenderEntryGrass *entry = (RenderEntryGrass *)(render_push_buffer->base + render_push_buffer->used);
    render_push_buffer->used += sizeof(*entry);

    assert(render_push_buffer->used <= render_push_buffer->total_size);
    assert(vertex_count != 0 && index_count != 0);

    entry->header.type = RenderEntryType_Grass;
    entry->p = p;
    entry->dim = dim;
    entry->color = color;

    entry->should_cast_shadow = should_cast_shadow;

    entry->vertex_buffer_offset = push_vertex_data(render_push_buffer, vertices, vertex_count);
    entry->index_buffer_offset = push_index_data(render_push_buffer, indices, index_count);
    entry->index_count = index_count;
}

internal void
push_quad(PlatformRenderPushBuffer *render_push_buffer, v3 min, v3 max, v3 color)
{
}














