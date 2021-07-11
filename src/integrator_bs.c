/**
 * @file 	integrator.c
 * @brief 	BS integration scheme.
 * @author 	Hanno Rein <hanno@hanno-rein.de>
 * @details	This file implements the Gragg-Bulirsch-Stoer integration scheme.  
 *          It is a reimplementation of the fortran code by E. Hairer and G. Wanner.
 *          The starting point was the JAVA implementation in hipparchus:
 *          https://github.com/Hipparchus-Math/hipparchus/blob/master/hipparchus-ode/src/main/java/org/hipparchus/ode/nonstiff/GraggBulirschStoerIntegrator.java
 *
 * @section 	LICENSE
 * Copyright (c) 2021 Hanno Rein
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2004, Ernst Hairer
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <float.h> // for DBL_MAX
#include "rebound.h"
#include "integrator_bs.h"
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void setInterpolationControl(struct reb_simulation_integrator_bs* ri_bs, int useInterpolationErrorForControl, int mudifControlParameter) {

    ri_bs->useInterpolationError = useInterpolationErrorForControl;

    if ((mudifControlParameter <= 0) || (mudifControlParameter >= 7)) {
        ri_bs->mudif = 4;
    } else {
        ri_bs->mudif = mudifControlParameter;
    }

}


int tryStep(struct reb_simulation_integrator_bs* ri_bs, const double t0, const double* y0, const int y0_length, const double step, const int k, const double* scale, double** const f, double* const yMiddle, double* const yEnd) {

    const int    n        = ri_bs->sequence[k];
    const double subStep  = step / n;
    const double subStep2 = 2 * subStep;

    // first substep
    double t = t0 + subStep;
    for (int i = 0; i < y0_length; ++i) {
        yEnd[i] = y0[i] + subStep * f[0][i];
    }
    ri_bs->computeDerivatives(ri_bs, f[1], t, yEnd);
    //for (int i = 0; i < y0_length; ++i) {
    //    printf("%e             %e       \n", f[1][i], f[0][i]);
    //}

    // other substeps
    double* const yTmp = malloc(sizeof(double)*y0_length); // IMPROVE: should allocate this only once
    for (int j = 1; j < n; ++j) {

        if (2 * j == n) {
            // save the point at the middle of the step
            for (int i = 0; i < y0_length; ++i) {
                yMiddle[i] = yEnd[i];
            }
        }

        t += subStep;
        for (int i = 0; i < y0_length; ++i) {
            const double middle = yEnd[i];
            yEnd[i]       = y0[i] + subStep2 * f[j][i];
            yTmp[i]       = middle;
        }

        ri_bs->computeDerivatives(ri_bs, f[j + 1], t, yEnd);

        // stability check
        if (ri_bs->performStabilityCheck && (j <= ri_bs->maxChecks) && (k < ri_bs->maxIter)) {
            double initialNorm = 0.0;
            for (int l = 0; l < y0_length; ++l) {
                const double ratio = f[0][l] / scale[l];
                initialNorm += ratio * ratio;
            }
            double deltaNorm = 0.0;
            for (int l = 0; l < y0_length; ++l) {
                const double ratio = (f[j + 1][l] - f[0][l]) / scale[l];
                deltaNorm += ratio * ratio;
            }
            //printf("iii   %e %e\n",initialNorm, deltaNorm);
            if (deltaNorm > 4 * MAX(1.0e-15, initialNorm)) {
                return 0;
            }
        }

    }

    // correction of the last substep (at t0 + step)
    for (int i = 0; i < y0_length; ++i) {
        yEnd[i] = 0.5 * (yTmp[i] + yEnd[i] + subStep * f[n][i]);
    }

    free(yTmp);
    return 1;

}

void extrapolate(struct reb_simulation_integrator_bs* ri_bs, const int offset, const int k, double** const diag, double* const last, const int last_length) {
    // update the diagonal
    for (int j = 1; j < k; ++j) {
        for (int i = 0; i < last_length; ++i) {
            // Aitken-Neville's recursive formula
            diag[k - j - 1][i] = diag[k - j][i] +
                ri_bs->coeff[k + offset][j - 1] * (diag[k - j][i] - diag[k - j - 1][i]);
        }
    }

    // update the last element
    for (int i = 0; i < last_length; ++i) {
        // Aitken-Neville's recursive formula
        last[i] = diag[0][i] + ri_bs->coeff[k + offset][k - 1] * (diag[0][i] - last[i]);
        //printf("last = %e\n", last[i]);
    }
}

double ulp(double x){
    return nextafter(x, INFINITY) - x;
}

double getTolerance(struct reb_simulation_integrator_bs* ri_bs, int i, double scale){
    return ri_bs->scalAbsoluteTolerance + ri_bs->scalRelativeTolerance * scale;
}

void rescale(struct reb_simulation_integrator_bs* ri_bs, double* const y1, double* const y2, double* const scale, int scale_length) {
    for (int i = 0; i < scale_length; ++i) {
        //scale[i] = getTolerance(ri_bs, i, MAX(fabs(y1[i]), fabs(y2[i])));  //TODO
        scale[i] = getTolerance(ri_bs, i, 1.);
    }
} 
double filterStep(struct reb_simulation_integrator_bs* ri_bs, const double h, const int forward, const int acceptSmall){
    double filteredH = h;
    if (fabs(h) < ri_bs->minStep) {
        if (acceptSmall) {
            filteredH = forward ? ri_bs->minStep : -ri_bs->minStep;
        } else {
            printf("Error. Minimal stepsize reached during integration.");
            exit(0);
        }
    }

    if (filteredH > ri_bs->maxStep) {
        filteredH = ri_bs->maxStep;
    } else if (filteredH < -ri_bs->maxStep) {
        filteredH = -ri_bs->maxStep;
    }

    return filteredH;

}


double estimateError(struct reb_simulation_integrator_bs* ri_bs, struct ODEState previous, struct ODEState current, const double* scale, const int mu, double** yMidDots) {
    const int currentDegree = mu + 4;
    const int y0_length = previous.length;
    double** polynomials   = malloc(sizeof(double*)*(currentDegree + 1)); 
    for (int i = 0; i < currentDegree+1; ++i) {
        polynomials[i] = malloc(sizeof(double)*y0_length); 
    }
    // initialize the error factors array for interpolation
    double* errfac = NULL;
    if (currentDegree <= 4) {
        errfac = NULL;
    } else {
        errfac = malloc(sizeof(double)*(currentDegree - 4)); 
        for (int i = 0; i < currentDegree-4; ++i) {
            const int ip5 = i + 5;
            errfac[i] = 1.0 / (ip5 * ip5);
            const double e = 0.5 * sqrt(((double) (i + 1)) / ip5);
            for (int j = 0; j <= i; ++j) {
                errfac[i] *= e / (j + 1);
            }
        }
    }

    // compute the interpolation coefficients
    // computeCoefficients(mu);

    const double* y0    = previous.y;
    const double* y0Dot = previous.yDot;
    const double* y1    = current.y;
    const double* y1Dot = current.yDot;

    const double h = current.t - previous.t;
    for (int i = 0; i < y0_length; ++i) {

        const double yp0   = h * y0Dot[i];
        const double yp1   = h * y1Dot[i];
        const double ydiff = y1[i] - y0[i];
        const double aspl  = ydiff - yp1;
        const double bspl  = yp0 - ydiff;

        polynomials[0][i] = y0[i];
        polynomials[1][i] = ydiff;
        polynomials[2][i] = aspl;
        polynomials[3][i] = bspl;

        if (mu < 0) {
            break;
        }

        // compute the remaining coefficients
        const double ph0 = 0.5 * (y0[i] + y1[i]) + 0.125 * (aspl + bspl);
        polynomials[4][i] = 16 * (yMidDots[0][i] - ph0);

        if (mu > 0) {
            const double ph1 = ydiff + 0.25 * (aspl - bspl);
            polynomials[5][i] = 16 * (yMidDots[1][i] - ph1);

            if (mu > 1) {
                const double ph2 = yp1 - yp0;
                polynomials[6][i] = 16 * (yMidDots[2][i] - ph2 + polynomials[4][i]);

                if (mu > 2) {
                    const double ph3 = 6 * (bspl - aspl);
                    polynomials[7][i] = 16 * (yMidDots[3][i] - ph3 + 3 * polynomials[5][i]);

                    for (int j = 4; j <= mu; ++j) {
                        const double fac1 = 0.5 * j * (j - 1);
                        const double fac2 = 2 * fac1 * (j - 2) * (j - 3);
                        polynomials[j+4][i] = 16 * (yMidDots[j][i] + fac1 * polynomials[j+2][i] - fac2 * polynomials[j][i]);
                    }

                }
            }
        }
    }




    double error = 0;
    if (currentDegree >= 5) {
        for (int i = 0; i < y0_length; ++i) {
            const double e = polynomials[currentDegree][i] / scale[i];
            error += e * e;
            //printf("error eeee %e\n",e);
        }
        error = sqrt(error / y0_length) * errfac[currentDegree - 5];
    }

    for (int i = 0; i < currentDegree+1; ++i) {
        free(polynomials[i]);
    }
    free(polynomials);
    free(errfac);

    return error;
}

void allocateState(struct ODEState* const state){
    if (!state->length){
        return;
    }
    if (state->y){
        free(state->y);
    }
    if (state->yDot){
        free(state->yDot);
    }
    state->y = malloc(sizeof(double)*state->length);
    state->yDot = malloc(sizeof(double)*state->length);
}


void fromSimulationToState(struct reb_simulation* const r, struct ODEState* const state){
    state->length = r->N*3*2;
    allocateState(state);

    state->t = r->t;
    for (int i=0; i<r->N; i++){
        const struct reb_particle p = r->particles[i];
        state->y[i*6+0] = p.x;
        state->y[i*6+1] = p.y;
        state->y[i*6+2] = p.z;
        state->y[i*6+3] = p.vx;
        state->y[i*6+4] = p.vy;
        state->y[i*6+5] = p.vz;
    }
}

void fromStateToSimulation(struct reb_simulation* const r, struct ODEState* const state){
    if (r->N*3*2 != state->length){
        printf("Error. Size mismatch.");
        return;
    }
    for (int i=0; i<r->N; i++){
         struct reb_particle* const p = &(r->particles[i]);
         p->x  = state->y[i*6+0];
         p->y  = state->y[i*6+1];
         p->z  = state->y[i*6+2];
         p->vx = state->y[i*6+3];
         p->vy = state->y[i*6+4];
         p->vz = state->y[i*6+5];
    }
    r->t = state->t;
}

void computeDerivativesGravity(struct reb_simulation_integrator_bs* const ri_bs, double* const yDot, double const t, const double* const y){
    struct reb_simulation* const r = (struct reb_simulation* const)ri_bs->ref;
    for (int i=0; i<r->N; i++){
         struct reb_particle* const p = &(r->particles[i]);
         p->x  = y[i*6+0];
         p->y  = y[i*6+1];
         p->z  = y[i*6+2];
         p->vx = y[i*6+3];
         p->vy = y[i*6+4];
         p->vz = y[i*6+5];
    }
    reb_update_acceleration(r);
    for (int i=0; i<r->N; i++){
        const struct reb_particle p = r->particles[i];
        yDot[i*6+0] = p.vx;
        yDot[i*6+1] = p.vy;
        yDot[i*6+2] = p.vz;
        yDot[i*6+3] = p.ax;
        yDot[i*6+4] = p.ay;
        yDot[i*6+5] = p.az;
    }
}

void prepare_memory(struct reb_simulation_integrator_bs* ri_bs, const int length){
    // reinitialize the arrays
    int maxOrder = 18;
    int sequence_length = maxOrder / 2;

    if ((ri_bs->sequence == NULL) || (ri_bs->sequence_length != sequence_length)) {
        // all arrays should be reallocated with the right size
        ri_bs->sequence        = realloc(ri_bs->sequence,sizeof(int)*sequence_length);
        ri_bs->costPerStep     = realloc(ri_bs->costPerStep,sizeof(int)*sequence_length);
        ri_bs->coeff           = realloc(ri_bs->coeff,sizeof(double*)*sequence_length);
        ri_bs->sequence_length = sequence_length;
        for (int k = ri_bs->sequence_length; k < sequence_length; ++k) {
            ri_bs->coeff[k] = NULL;
        }
        ri_bs->costPerTimeUnit = realloc(ri_bs->costPerTimeUnit,sizeof(double)*sequence_length);
        ri_bs->optimalStep     = realloc(ri_bs->optimalStep,sizeof(double)*sequence_length);
    }

    // step size sequence: 2, 6, 10, 14, ...
    for (int k = 0; k < sequence_length; ++k) {
        ri_bs->sequence[k] = 4 * k + 2;
    }

    // initialize the order selection cost array
    // (number of function calls for each column of the extrapolation table)
    ri_bs->costPerStep[0] = ri_bs->sequence[0] + 1;
    for (int k = 1; k < sequence_length; ++k) {
        ri_bs->costPerStep[k] = ri_bs->costPerStep[k - 1] + ri_bs->sequence[k];
    }

    // initialize the extrapolation tables
    for (int k = 1; k < sequence_length; ++k) {
        ri_bs->coeff[k] = malloc(sizeof(double)*k);
        for (int l = 0; l < k; ++l) {
            double ratio = ((double) ri_bs->sequence[k]) / ri_bs->sequence[k - l - 1];
            ri_bs->coeff[k][l] = 1.0 / (ratio * ratio - 1.0);
        }
    }


    setInterpolationControl(ri_bs, 1, -1);

    if (length>ri_bs->allocatedN){
        // create some internal working arrays
        ri_bs->y         = malloc(sizeof(double)*length); // TODO free
        ri_bs->y1        = malloc(sizeof(double)*length); // TODO free
        ri_bs->diagonal  = malloc(sizeof(double*)*(ri_bs->sequence_length - 1)); // TODO free
        ri_bs->y1Diag    = malloc(sizeof(double*)*(ri_bs->sequence_length - 1)); // TODO free

        for (int k = 0; k < ri_bs->sequence_length - 1; ++k) {
            ri_bs->diagonal[k] = malloc(sizeof(double)*length); // TODO free
            ri_bs->y1Diag[k]   = malloc(sizeof(double)*length); // TODO free
        }

        ri_bs->fk = malloc(sizeof(double**)*ri_bs->sequence_length); // TODO free
        for (int k = 0; k < ri_bs->sequence_length; ++k) {
            ri_bs->fk[k] = malloc(sizeof(double*)*(ri_bs->sequence[k] + 1)); // TODO free
            // Note: memory at i=0 is shared with initial state (setup later)
            for(int i=1; i<ri_bs->sequence[k] + 1; i++){
                ri_bs->fk[k][i] = malloc(sizeof(double)*length); // TODO free
            }
        }

        // scaled derivatives at the middle of the step $\tau$
        // (element k is $h^{k} d^{k}y(\tau)/dt^{k}$ where h is step size...)
        ri_bs->yMidDots = malloc(sizeof(double*)*(1 + 2 * ri_bs->sequence_length)); // TODO free
        for (int k = 0; k < 1+2*ri_bs->sequence_length; ++k) {
            ri_bs->yMidDots[k] = malloc(sizeof(double)*length); // TODO free
        }

        ri_bs->scale = malloc(sizeof(double)*length); // TODO free
    }

    ri_bs->costPerTimeUnit[0]       = 0;
}

void singleStep(struct reb_simulation_integrator_bs* ri_bs, const int firstOrLastStep){
    
    // initial order selection
    const double tol    = ri_bs->scalRelativeTolerance;
    const double log10R = log10(MAX(1.0e-10, tol));
    int targetIter = MAX(1, MIN(ri_bs->sequence_length - 2, (int) floor(0.5 - 0.6 * log10R)));

    double  maxError                 = DBL_MAX;
    int previousRejected         = 0;

    const int forward = ri_bs->hNew >= 0.;
    int y_length = ri_bs->initialState.length;
    double error;
    int reject = 0;

    for (int i=0; i<y_length; i++){
        ri_bs->y[i] = ri_bs->initialState.y[i];
    }

    for (int k = 0; k < ri_bs->sequence_length; ++k) {
        // first evaluation, at the beginning of the step
        // all sequences start from the same point, so we share the derivatives
        ri_bs->fk[k][0] = ri_bs->initialState.yDot;
    }

    ri_bs->stepSize = ri_bs->hNew;

    const double nextT = ri_bs->initialState.t + ri_bs->stepSize;

    // iterate over several substep sizes
    int k = -1;
    for (int loop = 1; loop; ) {

        ++k;
        
        //printf("loop k=%d\n",k);

        // modified midpoint integration with the current substep
        if ( ! tryStep(ri_bs, ri_bs->initialState.t, ri_bs->y, y_length, ri_bs->stepSize, k, ri_bs->scale, ri_bs->fk[k],
                    (k == 0) ? ri_bs->yMidDots[0] : ri_bs->diagonal[k - 1],
                    (k == 0) ? ri_bs->y1 : ri_bs->y1Diag[k - 1])) {

            // the stability check failed, we reduce the global step
            printf("old step  %e\n",ri_bs->hNew);
            ri_bs->hNew   = fabs(filterStep(ri_bs, ri_bs->stepSize * ri_bs->stabilityReduction, forward, 0));
            printf("new step  %e\n",ri_bs->hNew);
            reject = 1;
            loop   = 0;

        } else {

            // the substep was computed successfully
            if (k > 0) {

                // extrapolate the state at the end of the step
                // using last iteration data
                extrapolate(ri_bs, 0, k, ri_bs->y1Diag, ri_bs->y1, y_length);
                rescale(ri_bs, ri_bs->y, ri_bs->y1, ri_bs->scale, y_length);

                // estimate the error at the end of the step.
                error = 0;
                for (int j = 0; j < y_length; ++j) {
                    const double e = fabs(ri_bs->y1[j] - ri_bs->y1Diag[0][j]) / ri_bs->scale[j];
                    //printf("error %e   %e\n",ri_bs->y1[j] , ri_bs->y1Diag[0][j]);
                    error += e * e;
                }
                error = sqrt(error / y_length);
                if (isnan(error)) {
                    printf("Error. NaN appearing during integration.");
                    exit(0);
                }
                printf("(k=%d) error = %e\n",k, error);

                if ((error > 1.0e15) || ((k > 1) && (error > maxError))) {
                    // error is too big, we reduce the global step
                    printf("old step  %e\n",ri_bs->hNew);
                    ri_bs->hNew   = fabs(filterStep(ri_bs, ri_bs->stepSize * ri_bs->stabilityReduction, forward, 0));
                    printf("new step  %e\n",ri_bs->hNew);
                    reject = 1;
                    loop   = 0;
                } else {

                    maxError = MAX(4 * error, 1.0);

                    // compute optimal stepsize for this order
                    const double exp = 1.0 / (2 * k + 1);
                    double fac = ri_bs->stepControl2 / pow(error / ri_bs->stepControl1, exp);
                    const double power = pow(ri_bs->stepControl3, exp);
                    fac = MAX(power / ri_bs->stepControl4, MIN(1. / power, fac));
                    const int acceptSmall = k < targetIter;
                    ri_bs->optimalStep[k]     = fabs(filterStep(ri_bs, ri_bs->stepSize * fac, forward, acceptSmall));
                    ri_bs->costPerTimeUnit[k] = ri_bs->costPerStep[k] / ri_bs->optimalStep[k];

                    // check convergence
                    switch (k - targetIter) {

                        case -1 :
                            if ((targetIter > 1) && ! previousRejected) {

                                // check if we can stop iterations now
                                if (error <= 1.0) {
                                    // convergence have been reached just before targetIter
                                    loop = 0;
                                } else {
                                    // estimate if there is a chance convergence will
                                    // be reached on next iteration, using the
                                    // asymptotic evolution of error
                                    const double ratio = ((double) ri_bs->sequence[targetIter] * ri_bs->sequence[targetIter + 1]) / (ri_bs->sequence[0] * ri_bs->sequence[0]);
                                    if (error > ratio * ratio) {
                                        // we don't expect to converge on next iteration
                                        // we reject the step immediately and reduce order
                                        reject = 1;
                                        loop   = 0;
                                        targetIter = k;
                                        if ((targetIter > 1) &&
                                                (ri_bs->costPerTimeUnit[targetIter - 1] <
                                                 ri_bs->orderControl1 * ri_bs->costPerTimeUnit[targetIter])) {
                                            --targetIter;
                                        }
                                        printf("old step  %e\n",ri_bs->hNew);
                                        ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[targetIter], forward, 0);
                                        printf("new step  %e\n",ri_bs->hNew);
                                    }
                                }
                            }
                            break;

                        case 0:
                            if (error <= 1.0) {
                                // convergence has been reached exactly at targetIter
                                loop = 0;
                            } else {
                                // estimate if there is a chance convergence will
                                // be reached on next iteration, using the
                                // asymptotic evolution of error
                                const double ratio = ((double) ri_bs->sequence[k + 1]) / ri_bs->sequence[0];
                                if (error > ratio * ratio) {
                                    // we don't expect to converge on next iteration
                                    // we reject the step immediately
                                    printf("rejected no conv expected\n");
                                    reject = 1;
                                    loop = 0;
                                    if ((targetIter > 1) &&
                                            (ri_bs->costPerTimeUnit[targetIter - 1] <
                                             ri_bs->orderControl1 * ri_bs->costPerTimeUnit[targetIter])) {
                                        --targetIter;
                                    }
                                    ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[targetIter], forward, 0);
                                }
                            }
                            break;

                        case 1 :
                            if (error > 1.0) {
                                printf("rejected large error\n");
                                reject = 1;
                                if ((targetIter > 1) &&
                                        (ri_bs->costPerTimeUnit[targetIter - 1] <
                                         ri_bs->orderControl1 * ri_bs->costPerTimeUnit[targetIter])) {
                                    --targetIter;
                                }
                                ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[targetIter], forward, 0);
                            }
                            loop = 0;
                            break;

                        default :
                            if (firstOrLastStep && (error <= 1.0)) {
                                loop = 0;
                            }
                            break;

                    }

                }
            }
        }
    }

    // dense output handling
    double hInt = ri_bs->maxStep;
    if (! reject) {

        // extrapolate state at middle point of the step
        for (int j = 1; j <= k; ++j) {
            extrapolate(ri_bs, 0, j, ri_bs->diagonal, ri_bs->yMidDots[0], y_length);
        }

        const int mu = 2 * k - ri_bs->mudif + 3;

        for (int l = 0; l < mu; ++l) {

            // derivative at middle point of the step
            const int l2 = l / 2;
            double factor = pow(0.5 * ri_bs->sequence[l2], l);
            int fk_l2_length = ri_bs->sequence[l2] + 1;
            int middleIndex = fk_l2_length / 2;
            for (int i = 0; i < y_length; ++i) {
                ri_bs->yMidDots[l + 1][i] = factor * ri_bs->fk[l2][middleIndex + l][i];
            }
            for (int j = 1; j <= k - l2; ++j) {
                factor = pow(0.5 * ri_bs->sequence[j + l2], l);
                int fk_l2j_length = ri_bs->sequence[l2+j] + 1;
                middleIndex = fk_l2j_length / 2;
                for (int i = 0; i < y_length; ++i) {
                    ri_bs->diagonal[j - 1][i] = factor * ri_bs->fk[l2 + j][middleIndex + l][i];
                }
                extrapolate(ri_bs, l2, j, ri_bs->diagonal, ri_bs->yMidDots[l + 1], y_length);
            }
            for (int i = 0; i < y_length; ++i) {
                ri_bs->yMidDots[l + 1][i] *= ri_bs->stepSize;
            }

            // compute centered differences to evaluate next derivatives
            for (int j = (l + 1) / 2; j <= k; ++j) {
                int fk_j_length = ri_bs->sequence[j] + 1;
                for (int m = fk_j_length - 1; m >= 2 * (l + 1); --m) {
                    for (int i = 0; i < y_length; ++i) {
                        ri_bs->fk[j][m][i] -= ri_bs->fk[j][m - 2][i];
                    }
                }
            }

        }

        // state at end of step
        ri_bs->finalState.t = nextT;
        ri_bs->finalState.y = ri_bs->y1;
        ri_bs->finalState.length = y_length;
        ri_bs->computeDerivatives(ri_bs, ri_bs->finalState.yDot, ri_bs->finalState.t, ri_bs->finalState.y);

        if (mu >= 0 && ri_bs->useInterpolationError) {
            // use the interpolation error to limit stepsize
            const double interpError = estimateError(ri_bs, ri_bs->initialState, ri_bs->finalState, ri_bs->scale, mu, ri_bs->yMidDots);
            hInt = fabs(ri_bs->stepSize / MAX(pow(interpError, 1.0 / (mu + 4)), 0.01));
            if (interpError > 10.0) {
                printf("old step  %e\n",ri_bs->hNew);
                ri_bs->hNew   = filterStep(ri_bs, hInt, forward, 0);
                printf("new step  %e\n",ri_bs->hNew);
                printf("rejected large interpolation error\n");
                reject = 1;
            }
        }

    }

    if (! reject) {
        struct ODEState tmp;
        tmp = ri_bs->initialState;
        ri_bs->initialState = ri_bs->finalState; 
        ri_bs->finalState = tmp;

        int optimalIter;
        if (k == 1) {
            optimalIter = 2;
            if (previousRejected) {
                optimalIter = 1;
            }
        } else if (k <= targetIter) {
            optimalIter = k;
            if (ri_bs->costPerTimeUnit[k - 1] < ri_bs->orderControl1 * ri_bs->costPerTimeUnit[k]) {
                optimalIter = k - 1;
            } else if (ri_bs->costPerTimeUnit[k] < ri_bs->orderControl2 * ri_bs->costPerTimeUnit[k - 1]) {
                optimalIter = MIN(k + 1, ri_bs->sequence_length - 2);
            }
        } else {
            optimalIter = k - 1;
            if ((k > 2) && (ri_bs->costPerTimeUnit[k - 2] < ri_bs->orderControl1 * ri_bs->costPerTimeUnit[k - 1])) {
                optimalIter = k - 2;
            }
            if (ri_bs->costPerTimeUnit[k] < ri_bs->orderControl2 * ri_bs->costPerTimeUnit[optimalIter]) {
                optimalIter = MIN(k, ri_bs->sequence_length - 2);
            }
        }

        if (previousRejected) {
            // after a rejected step neither order nor stepsize
            // should increase
            targetIter = MIN(optimalIter, k);
            ri_bs->hNew = MIN(fabs(ri_bs->stepSize), ri_bs->optimalStep[targetIter]);
        } else {
            // stepsize control
            if (optimalIter <= k) {
                ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[optimalIter], forward, 0);
            } else {
                if ((k < targetIter) &&
                        (ri_bs->costPerTimeUnit[k] < ri_bs->orderControl2 * ri_bs->costPerTimeUnit[k - 1])) {
                    ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[k] * ri_bs->costPerStep[optimalIter + 1] / ri_bs->costPerStep[k], forward, 0);
                } else {
                    ri_bs->hNew = filterStep(ri_bs, ri_bs->optimalStep[k] * ri_bs->costPerStep[optimalIter] / ri_bs->costPerStep[k], forward, 0);
                }
            }

            targetIter = optimalIter;

        }
    }

    ri_bs->hNew = MIN(ri_bs->hNew, hInt);
    if (! forward) {
        ri_bs->hNew = -ri_bs->hNew;
    }

    if (reject) {
        previousRejected = 1;
        printf("Step rejected\n");
    } else {
        previousRejected = 0;
    }
}



void reb_integrator_bs_part1(struct reb_simulation* r){
}
void reb_integrator_bs_part2(struct reb_simulation* r){
    double t_initial = r->t;

    struct reb_simulation_integrator_bs* ri_bs = &(r->ri_bs);

    int firstStep = 0;
    const int length = r->N*3*2;
    if (ri_bs->y==NULL){
        prepare_memory(ri_bs, length);
        firstStep = 1;
    }
    ri_bs->computeDerivatives       = computeDerivativesGravity;
    ri_bs->ref    = r;
    ri_bs->hNew   = r->dt;

    fromSimulationToState(r, &ri_bs->initialState);
    ri_bs->finalState.length = length;
    allocateState(&ri_bs->finalState);

    // initial scaling
    rescale(ri_bs, ri_bs->initialState.y, ri_bs->initialState.y, ri_bs->scale, length);
    ri_bs->computeDerivatives(ri_bs, ri_bs->initialState.yDot, r->t, ri_bs->initialState.y);
    singleStep(ri_bs, firstStep || r->status==REB_RUNNING_LAST_STEP);

    fromStateToSimulation(r, &ri_bs->initialState);
    
    r->dt = ri_bs->hNew;
    r->dt_last_done = t_initial - r->t;


}

void reb_integrator_bs_synchronize(struct reb_simulation* r){
    // Do nothing.
}


void reb_integrator_bs_reset(struct reb_simulation* r){
    struct reb_simulation_integrator_bs* ri_bs = &(r->ri_bs);
    // Defaul settings
    ri_bs->scalAbsoluteTolerance= 1e-5;
    ri_bs->scalRelativeTolerance= 1e-5;
    ri_bs->maxStep              = 10; // Note: always positive
    ri_bs->minStep              = 1e-5; // Note: always positive
    ri_bs->performStabilityCheck= 1;
    ri_bs->maxIter              = 2;
    ri_bs->maxChecks            = 1;
    ri_bs->stabilityReduction   = 0.5;
    ri_bs->stepControl1         = 0.65;
    ri_bs->stepControl2         = 0.94;
    ri_bs->stepControl3         = 0.02;
    ri_bs->stepControl4         = 4.0;
    ri_bs->orderControl1        = 0.8;
    ri_bs->orderControl2        = 0.9;

}
