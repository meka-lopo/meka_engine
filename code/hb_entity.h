#ifndef HB_ENTITY_H
#define HB_ENTITY_H

enum EntityType
{
    Entity_Type_Null,
    Entity_Type_AABB,
    Entity_Type_Floor,
    Entity_Type_Grass,
};

enum EntityFlag
{
    Entity_Flag_Null = 0,
    Entity_Flag_Movable = 2,
    Entity_Flag_Collides = 4,
};

struct Entity
{
    EntityType type;
    u32 flags;

    v3 p; // TODO(gh) such objects like rigid body does not make use of this!!!
    v3 color;

    // TODO(joon) some kind of entity system, 
    // so that we don't have to store entity_specific things in all of the entities
    MassAgg mass_agg;
    RigidBody rb; // TODO(joon) make this a pointer!

    // TODO(joon) CollisionVolumeGroup!
    CollisionVolumeCube cv;

    AABB aabb; 
};

#endif
