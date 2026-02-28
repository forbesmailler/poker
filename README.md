# poker

## Setup

```bash
mamba env create -f environment.yaml
mamba activate poker
```

## Development

```bash
invoke format   # ruff format + check
invoke test     # pytest with coverage
invoke all      # both
```
