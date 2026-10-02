# Contributing

This repo holds CCS example projects and reference notes for the LAUNCHXL-F28379D and
its BoosterPacks. Anyone in the lab can propose changes. Every change reaches `main`
through a pull request reviewed by the maintainer, @PhilBaik.

## Setup

1. Install CCS 21.0.1 with the C2000 compiler TI v25.11.1.LTS (see
   [28379D_demo/README.md](28379D_demo/README.md), section 1).
2. Clone the repo, then import projects from it in place: *File → Import Projects*. Do
   not copy them into the workspace.
3. The TI PDFs are not in git (`*.pdf` is ignored). Each `refs/docs/*/README.md` lists
   them by TI literature number; download them from ti.com into the same folder.

## Workflow

`main` is protected: you cannot push to it directly, and each pull request needs one
approval.

```bash
git switch main && git pull                 # start from the latest main
git switch -c <your-name>/<topic>           # e.g. jkim/adc-calibration
# ...edit, build, test...
git add <files>
git commit -m "Fix VDC scaling for 3.0 V ADC reference"
git push -u origin <your-name>/<topic>
```

Then open a pull request on GitHub and fill in the template. If the reviewer asks for
changes, push more commits to the same branch; the pull request updates by itself.
After it is merged, delete the branch and start the next change from a fresh `main`.

Keep each pull request to one topic. A fix and a new example go in separate pull
requests.

## Rules for the code

- **Leave `device/` alone.** It is TI code copied from C2000Ware v26.02.00.00 (DriverLib,
  `device.c/.h`). Change it only in a dedicated pull request that updates the C2000Ware
  version.
- **New project = duplicate an existing one inside CCS** (project context menu → Copy,
  paste under a new name). A Finder copy keeps the old project name. See
  [28379D_demo/README.md](28379D_demo/README.md), section 6.
- Name application files after their project (`<project>_main.c`, `<project>.h`), as
  the existing examples do.
- Keep the `_LAUNCHXL_F28379D` predefined symbol. Without it the clock runs at half
  speed and the LED, SCI and CAN pins are wrong.
- **Build before you push**, in both configurations (`CPU1_RAM` and `CPU1_FLASH`), with
  no new warnings.
- Pin numbers, scale factors and sensor values must agree with the board notes in
  `refs/docs/`. If you find an error in those notes, fix the note in the same pull
  request and cite the PDF page.

## Do not commit

- Build output (`CPU1_RAM/`, `CPU1_FLASH/`, `*.out`, `*.obj`, `*.map`) or TI PDFs. These are
  already ignored.
- Files CCS generates per machine (`.mcp.json`, `.theia/`, `.claude/`, `CLAUDE.md` inside
  a project). These are ignored too. If `git status` shows one anyway, ask before
  adding it.
- Personal workspace files, large data captures, or scope screenshots without context.
  A screenshot that backs a pull request belongs in the pull request description.

Run `git status` before every commit and add files by name, not with `git add .`.

## Commit messages

Write one summary line in the imperative ("Add", "Fix", "Update"), under about 70
characters. Add a blank line and a short body when the reason is not obvious. Look at
`git log` for examples.

## Hardware safety

The BOOSTXL-3PhGaNInv switches up to 60 V.

- Bring up new firmware with the gate driver disabled (`nEn_uC` high, which is the
  default) and the bench supply current-limited.
- The board's overcurrent protection does not latch and does not tell the MCU it
  tripped. Do not rely on it as your only protection.
- Remove the BoosterPack 3.3 V jumper when the LaunchPad is powered from USB. See
  [refs/docs/3ph_boostxl/README.md](refs/docs/3ph_boostxl/README.md).
