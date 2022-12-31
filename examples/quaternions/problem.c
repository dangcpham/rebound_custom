/**
 * Using Quaternions in REBOUND
 * 
 * A simple example showing various use cases of quaternions
 */
#include "rebound.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    struct reb_simulation* r = reb_create_simulation();

    reb_add_fmt(r, "m", 1.);                // Central object
    reb_add_fmt(r, "m a e", 1e-3, 1., 0.1); // Jupiter mass planet


    struct reb_quat q1 = reb_quat_identity();
    struct reb_vec3d v = {.x = 1, .y = 2, .z = 3};

    printf("v = %.5f %.5f %.5f\n", v.x, v.y, v.z);
    v = reb_quat_act(q1,v);
    printf("v = %.5f %.5f %.5f\n", v.x, v.y, v.z);

    struct reb_vec3d axis = {.x = 1, .y = 0, .z = 0};
    q1 = reb_quat_from_angle_axis(M_PI/2.12, axis);
    v = reb_quat_act(q1, v);
    printf("v = %.5f %.5f %.5f\n", v.x, v.y, v.z);
    
    axis.x = 0; axis.y = 1; axis.z = 0;
    struct reb_quat q2 = reb_quat_from_angle_axis(M_PI/2.12, axis);
    v = reb_quat_act(q2, v);
    printf("v = %.5f %.5f %.5f\n", v.x, v.y, v.z);
    
    struct reb_quat q3 = reb_quat_mul(q2, q1);
    struct reb_vec3d v3 = {.x = 1, .y = 2, .z = 3};
    v3 = reb_quat_act(q3, v3);
    printf("v3 = %.5f %.5f %.5f\n", v3.x, v3.y, v3.z);
    
    struct reb_quat q4 = reb_quat_inverse(q3);
    v3 = reb_quat_act(q4, v3);
    printf("v3 = %.5f %.5f %.5f\n", v3.x, v3.y, v3.z);

    printf("particle = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r->particles[1].x, r->particles[1].y, r->particles[1].z, r->particles[1].vx, r->particles[1].vy, r->particles[1].vz);
    reb_simulation_rotate(r, q3);
    printf("particle = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r->particles[1].x, r->particles[1].y, r->particles[1].z, r->particles[1].vx, r->particles[1].vy, r->particles[1].vz);
    reb_simulation_rotate(r, q4);
    printf("particle = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r->particles[1].x, r->particles[1].y, r->particles[1].z, r->particles[1].vx, r->particles[1].vy, r->particles[1].vz);

    
    reb_free_simulation(r);


    // new tests
    printf("  \n\n");
    r = reb_create_simulation();
    struct reb_simulation* r2 = reb_create_simulation();

    reb_add_fmt(r, "m", 1.);                // Central object
    reb_add_fmt(r2, "m", 1.);                // Central object
    double Omega = 0.12;
    double inc = 0.23;
    double omega = 0.345;
    reb_add_fmt(r, "a e Omega inc omega ", 1., 0.00000001, Omega, inc, omega); 
    reb_add_fmt(r2, "a e", 1., 0.00000001); 
    printf("r->particle  = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r->particles[1].x, r->particles[1].y, r->particles[1].z, r->particles[1].vx, r->particles[1].vy, r->particles[1].vz);
    printf("r2->particle = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r2->particles[1].x, r2->particles[1].y, r2->particles[1].z, r2->particles[1].vx, r2->particles[1].vy, r2->particles[1].vz);
    struct reb_quat q5 = reb_quat_from_orbital(Omega, inc, omega);
    reb_simulation_rotate(r2, q5);
    printf("r2->particle = %.5f %.5f %.5f   %.5f %.5f %.5f\n", r2->particles[1].x, r2->particles[1].y, r2->particles[1].z, r2->particles[1].vx, r2->particles[1].vy, r2->particles[1].vz);
    

    reb_free_simulation(r);
    reb_free_simulation(r2);
}

