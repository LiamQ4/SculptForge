# AI services: privacy and model licences

Every AI feature in SculptForge is **optional and off until you use it**. SculptForge has no
telemetry, no account and no servers of its own. When you use an AI feature, SculptForge talks
**directly** to the service you picked; nothing goes anywhere else.

## What is sent where

| Feature | Service (your choice) | What is sent | Key |
|---|---|---|---|
| Brush Lab | Anthropic (Claude), or any OpenAI-compatible endpoint such as a local Ollama / LM Studio | Your brush description and the current brush settings (text only) | Your API key; none for local models |
| Generate 3D (image) | Hugging Face Spaces (free), fal.ai, Meshy, or your own command | The image you chose, cropped and resized to at most 1024 px | Optional HF token; your fal.ai / Meshy key |
| Generate 3D (text) | Hugging Face Spaces (free), fal.ai, Meshy | Your text description | As above |
| Send to Unreal | Your own Unreal Editor (local network) | The exported FBX | None |

Keys are stored in your local settings file (`ai.json`, owner-only permissions) or read from
environment variables (`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `HF_TOKEN`, `FAL_KEY`, `MESHY_API_KEY`).

## The free image-to-3D and text-to-3D models

The free option runs each model author's **own public demo** on Hugging Face's shared ZeroGPU
hardware. SculptForge isn't affiliated with Hugging Face or any of the model authors, and doesn't
host, bundle or redistribute any model. GPU time is rationed per user per day; a free Hugging Face
token raises the allowance.

**Each model has its own licence, and you're responsible for following it:**

| Model | Author / provider | Licence | Key terms (summary, not legal advice) |
|---|---|---|---|
| TRELLIS.2 | Microsoft (Space by Microsoft) | MIT | Permissive. |
| Hunyuan3D 2.1, Hunyuan3D 2, Hunyuan3D 2 mini Turbo | Tencent (Spaces by Tencent) | [Tencent Hunyuan Community License](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE) | **Not licensed for use in the European Union, the United Kingdom or South Korea.** Products over 1 million monthly active users need a separate licence from Tencent. Tencent claims no rights in your outputs. Outputs may not be used to improve other AI models. |
| Stable Fast 3D | Stability AI (Space by Stability AI) | [Stability AI Community License](https://huggingface.co/stabilityai/stable-fast-3d/blob/main/LICENSE.md) | Free, including commercial use, for individuals and organisations under US $1M annual revenue; above that, an enterprise licence is required. You own your outputs. "Powered by Stability AI". |

These summaries are for convenience only. Read the licences linked above before using outputs
commercially, and check where you are. If you're in the EU, UK or South Korea, choose TRELLIS.2,
Stable Fast 3D or a paid service instead of Hunyuan3D.

## Paid services

fal.ai and Meshy are paid services with their own terms of service and pricing; generations are
billed to your account with them. Model availability and names can change on their side.
