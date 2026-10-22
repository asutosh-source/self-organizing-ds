# Adaptive Data Lab web app: run and deploy

## Live website

https://asutosh-source.github.io/self-organizing-ds/

The site is a static HTML/CSS/JavaScript app. It needs no server, database, API key, or build step.

## Run locally

1. Download or clone the repository:
   `git clone https://github.com/asutosh-source/self-organizing-ds.git`
2. Open the repository folder.
3. Double-click `index.html` to open it in a modern browser.

For a local HTTP server (optional), run this from the repository folder with Python installed:

```bash
python -m http.server 8000
```

Then open http://localhost:8000.

## What is in the web app

- Interactive list playground with fixed, Move-to-Front, Transpose, Frequency Count, and Adaptive policies.
- Uniform, skewed, phase-change, and sequential-scan workloads.
- Benchmark explorer and comparison-over-time chart.
- CSV downloads for benchmark summary and timeline, plus a JSON experiment configuration export.
- Responsive layout and light/dark theme support.

## Reproducibility and scientific claims

The browser version is a JavaScript demonstration, not the native C executable. Its outputs should be labelled as browser-simulated until checked against the C implementation with matching workload, seed, policy and window settings. Browser timing is not evidence of native C performance. Do not claim universal speedups; report the tested workload and parameters.

## Deployment

The repository's GitHub Pages workflow builds from the repository root. Keep the public entry point at `index.html`. A push to `main` triggers the Pages workflow; check the Actions tab for a successful build/deploy. Changes may take a short time to appear, so refresh the site after the workflow finishes.

## Native C version

See the repository README for the C11 build and test commands. Install a C compiler and Make before running `make`, `make test`, or `make bench`.
