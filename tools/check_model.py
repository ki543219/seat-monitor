"""CI gate for the on-device model.

Fails if the model's input/output contract or ops change in a way the firmware does not
handle, if it grows past the flash budget, or if src/model_data.cpp is out of sync.

Usage: python tools/check_model.py model/person_detect.tflite src/model_data.cpp
"""
import re
import sys
from pathlib import Path

import tflite

MAX_BYTES = 400 * 1024
EXPECTED_INPUT = ([1, 96, 96, 1], "INT8")
EXPECTED_OUTPUT = ([1, 2], "INT8")
# Must match the MicroMutableOpResolver in src/detector.cpp.
SUPPORTED_OPS = {"AVERAGE_POOL_2D", "CONV_2D", "DEPTHWISE_CONV_2D", "RESHAPE", "SOFTMAX"}


def names(enum):
    return {v: k for k, v in vars(enum).items() if not k.startswith("_")}


def main(model_path: str, c_path: str) -> None:
    data = Path(model_path).read_bytes()
    errors = []

    if len(data) > MAX_BYTES:
        errors.append(f"model is {len(data)} bytes, budget {MAX_BYTES}")

    model = tflite.Model.GetRootAsModel(data, 0)
    graph = model.Subgraphs(0)
    types = names(tflite.TensorType)
    for label, idx, (shape, dtype) in (
        ("input", graph.Inputs(0), EXPECTED_INPUT),
        ("output", graph.Outputs(0), EXPECTED_OUTPUT),
    ):
        t = graph.Tensors(idx)
        got = (t.ShapeAsNumpy().tolist(), types[t.Type()])
        print(f"{label}: shape={got[0]} type={got[1]}")
        if got != (shape, dtype):
            errors.append(f"{label} is {got}, firmware expects {(shape, dtype)}")

    op_names = names(tflite.BuiltinOperator)
    ops = {
        op_names[model.OperatorCodes(graph.Operators(i).OpcodeIndex()).BuiltinCode()]
        for i in range(graph.OperatorsLength())
    }
    print(f"ops: {sorted(ops)}")
    if ops - SUPPORTED_OPS:
        errors.append(f"ops not registered in detector.cpp: {sorted(ops - SUPPORTED_OPS)}")

    c_src = Path(c_path).read_text(encoding="ascii")
    m = re.search(r"g_model_data_len = (\d+);", c_src)
    embedded = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", c_src))
    if not m or int(m.group(1)) != len(data) or embedded != data:
        errors.append(f"{c_path} is out of sync, run tools/model_to_c.py")

    print(f"size: {len(data)} bytes")
    if errors:
        sys.exit("FAIL\n- " + "\n- ".join(errors))
    print("OK")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
