from diffusers import SanaPipeline, SanaSprintPipeline
import torch
import argparse
import gui_server
import numpy as np

# Set up argument parser
parser = argparse.ArgumentParser(description='Generate an image using Sana model with a text prompt')
parser.add_argument('--port', type=int, default=5000, help='Port to run the server on (default: 5000)')
args = parser.parse_args()

pipeline = SanaPipeline.from_pretrained(
    "Efficient-Large-Model/Sana_1600M_1024px_diffusers",
    torch_dtype=torch.float16
)

pipeline.to("cuda:0")
pipeline.set_progress_bar_config(disable=True)

server = gui_server.InterfaceServer(args.port)
server.start()
gui_server.set_log_level("off")

width = 1024
height = 1024
server.initialise_video_stream(width, height)

state = gui_server.State()
while not state.stop:
    if state.is_playing:
        image = pipeline(prompt=state.prompt, num_inference_steps=state.steps, width=width, height=height, guidance_scale=state.value).images[0]

    # Need to re-send same image even if generation is paused so that video stream doesn't timeout:
    server.send_image(np.array(image), convert_to_bgr=True)

    if server.state_changed():
        state = server.consume_state()
        print(f"State updated: {state}")

server.stop()
