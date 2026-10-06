# Triple‑Model Inference Pipeline (ESP32‑S3 + TFLite Micro)

This project demonstrates a **three‑stage neural pipeline** running on an ESP32‑S3 using TensorFlow Lite Micro.
All models are embedded as C arrays and executed on‑device.

## Model Pipeline

The models are **not independent** — they form a chained pipeline:

1. **Input image → Model M1**
   - M1 takes the raw image tensor.
   - Produces:
     - **logits1** (classification output)
     - **high‑res features** (feature map / representation)

2. **High‑res features → Model M2**
   - M2 does **not** see the original image.
   - It takes the **high‑res features from M1** as input.
   - Produces:
     - **logits2** (mid‑resolution prediction)
     - **mid‑res features** (intermediate representation)

3. **High‑res + mid‑res features → Model M3**
   - M3 takes a **fusion** of:
     - high‑res features (from M1)
     - mid‑res features (from M2)
   - Produces:
     - **logits3** (final prediction)

So the data flow is:

```text
image
  └──> M1 → logits1, high-res
             └──> M2(high-res) → logits2, mid-res
                       └──> M3(high-res, mid-res) → logits3
```


## What the Program Currently Does

- Loads all three models from embedded arrays.
- Creates three interpreters and tensor arenas.
- Feeds a blank test image into M1.
- Runs:
    - M1 on the image.
    - M2 and M3 in sequence (conceptually) based on the designed pipeline.
- Prints all logits to the console for debugging.

Note: In the current test setup, the “high‑res” and “mid‑res” features are still internal to the models.
The README describes the intended logical relationship between the models, not necessarily the exact tensor wiring in this minimal demo.

>> TODO: Create pipeline m1 -> m2 -> m3.
