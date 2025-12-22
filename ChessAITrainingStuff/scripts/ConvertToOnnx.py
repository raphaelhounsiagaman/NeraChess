import torch

from TrainAI import ChessResNet

def main():
    chkpt_name = input("Wie heist die Datei?")
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    ckpt = torch.load(chkpt_name, map_location=device)
    model = ChessResNet(in_channels=19, filters=256, blocks=15).to(device)
    model.load_state_dict(ckpt['model'])

    model.eval()

    dummy = torch.randn(1, 19, 8, 8).to(device)
    torch._C._jit_set_profiling_executor(False)
    torch._C._jit_set_profiling_mode(False)

    torch.onnx.export(
    model,
    dummy,
    "model.onnx",
    input_names=["input"],
    output_names=["output"],
    dynamic_axes={
        "input": {0: "batch_size"},
        "output": {0: "batch_size"}
    },
    opset_version=17,
    do_constant_folding=True,
    verbose=False,
    )


if __name__ == '__main__':
    main()