from diffusers import SanaSprintPipeline
import torch
import argparse
import gui_server
import numpy as np

# Set up argument parser
parser = argparse.ArgumentParser(description='Generate an image using Sana model with a text prompt')
parser.add_argument('--port', type=int, default=5000, help='Port to run the server on (default: 5000)')
args = parser.parse_args()

pipeline = SanaSprintPipeline.from_pretrained(
    "Efficient-Large-Model/Sana_Sprint_1.6B_1024px_diffusers",
    torch_dtype=torch.bfloat16
)

pipeline.to("cuda:0")
pipeline.set_progress_bar_config(disable=True)

server = gui_server.InterfaceServer(args.port)
server.start()
gui_server.set_log_level("off")

test_image = np.zeros((1024, 1024, 3), dtype=np.uint8)
server.initialise_video_stream(test_image.shape[1], test_image.shape[0])

prompt="cat"
cfg_scale = 4.5
generate = True
while not server.get_state().stop:
    if generate:
        image = pipeline(prompt=prompt, num_inference_steps=2, width=1024, height=1024, guidance_scale=cfg_scale).images[0]

    # Need to re-send same image even if generation is paused so that video stream doesn't timeout:
    server.send_image(np.array(image), convert_to_bgr=True)

    if server.state_changed():
        state = server.consume_state()
        print(f"State updated:")
        print(f"Value: {state.value}")
        print(f"Prompt: {state.prompt}")
        print(f"Play/pause: {state.is_playing}")
        prompt = state.prompt
        cfg_scale = state.value
        generate = state.is_playing

server.stop()
