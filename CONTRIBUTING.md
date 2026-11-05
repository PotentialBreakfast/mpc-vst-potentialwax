# Ideas, bugs and helping out

Thanks for being here. This plugin gets better because people use them and tell me what's what, so **every kind of feedback is welcome**: ideas, bug reports, "this confused me", "I don't like how this works", or just "it works on my Live II".

## The easiest way: open an issue

Go to [**Issues → New issue**](../../issues/new/choose) and pick one:

- 💡 **Idea or request**: new features, changes, or a whole new plugin you'd like to see
- 🐛 **Bug**: something doesn't work, sounds wrong, crashes or looks broken

Short is fine. You don't need to be technical. Things that help me most:

- **Which MPC** you have, and the MPC OS version (Menu → Preferences → About or similar)
- **Which version** of the plugin
- **What you did, what you expected, and what happened.** A photo of the screen is gold.

## Criticism

Honest criticism is genuinely useful. If a control is confusing, a preset sounds bad or the whole idea doesn't work for you, say so. I'd much rather hear it than not. I'll take it in the spirit it's meant, and I ask everyone to do the same with each other.

## Please keep it friendly

Be kind to each other, assume good intent, and remember this is a free hobby project made in spare time. Rude or hostile comments will be removed.

## Sending code

Pull requests are welcome.

- **Talk first if it's big.** Open an issue before a large change so we can agree the approach.
- **Building and testing:** follow [BUILD.md](BUILD.md). Please run `test_port.sh` before sending.
- **Saved projects:** parameters are **append-only** (`vst/params.lock`). Add new ones at the end so existing projects keep loading.
- **Keep the look:** new pages should use the shared dark theme in `shared/potential-theme.conf`.
- **Licence:** by sending code you agree it's released under this repo's [MIT licence](LICENSE).

## Security or privacy

If you find something that could harm someone's MPC or data, please open an issue that says only "security, please contact me" and I'll get in touch, rather than posting the details publicly.
