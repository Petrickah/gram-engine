use crate::prelude::*;

/// Every small sphere bobs up and down: y follows y0 + A·sin(t + phase), written as a change per frame
/// (the derivative, A·cos(t + phase)·dt), since the component only holds the current y.
pub(crate) fn bob(time: Time, delta_time: DeltaTime, spheres: Query<Sphere>) {
    const AMPLITUDE: f32 = 0.25;
    let t = *time as f32;

    for (i, (_, mut sphere)) in spheres.iter_mut().enumerate() {
        if sphere.radius > 10.0 { continue; }   // the ground (radius 1000) stays put
        let phase = i as f32 * 0.7;
        sphere.center.y += AMPLITUDE * (t + phase).cos() * *delta_time;
    }
}
