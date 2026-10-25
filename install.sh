#!/usr/bin/env bash
# Optional installer. The benchmark remains a standalone C source file.
set -euo pipefail

PS_BENCHMARK_SOURCE_URL=${PS_BENCHMARK_SOURCE_URL:-https://raw.githubusercontent.com/santiagosilvestri/c-push-swap-benchmark/main/push_swap_benchmark.c}
benchmark_home=${PS_BENCHMARK_HOME:-${HOME:?HOME is not set}}
bin_dir="$benchmark_home/.local/bin"
target="$bin_dir/ps-benchmark"
build_dir=
run_mode=auto
path_configured=0

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [[ -n "$build_dir" && -d "$build_dir" ]]; then
        for name in push_swap_benchmark.c ps-benchmark; do
            if [[ -e "$build_dir/$name" ]]; then
                rm -- "$build_dir/$name"
            fi
        done
        rmdir -- "$build_dir" 2>/dev/null || true
    fi
    build_dir=
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Uninstallation uses only the installer's known paths and managed markers.
remove_path_block() {
    local profile staged
    profile=$1
    [[ -f "$profile" ]] || return 0
    grep -Fq '# >>> ps-benchmark PATH >>>' "$profile" || return 0
    staged=$(mktemp "$benchmark_home/.ps-benchmark-profile.XXXXXXXX")
    if ! awk '
        $0 == "# >>> ps-benchmark PATH >>>" {
            if (inside) exit 1
            inside = 1
            next
        }
        $0 == "# <<< ps-benchmark PATH <<<" {
            if (!inside) exit 1
            inside = 0
            next
        }
        !inside { print }
        END { if (inside) exit 1 }
    ' "$profile" > "$staged"; then
        rm -f -- "$staged"
        fail "Incomplete PATH markers in $profile; file was kept unchanged."
    fi
    # Preserve the profile's permissions and any existing symlink.
    if ! cat -- "$staged" > "$profile"; then
        rm -f -- "$staged"
        fail "Could not update $profile."
    fi
    rm -f -- "$staged"
    printf 'Removed managed PATH block from %s.\n' "$profile"
}

uninstall() {
    local profile cache_parent
    [[ "$benchmark_home" == /* && "$benchmark_home" != / ]] \
        || fail 'The installation home must be an absolute non-root directory.'
    [[ ! -d "$target" ]] || fail "$target is a directory; it was not removed."
    for profile in .bashrc .bash_profile .bash_login .profile .zshrc; do
        remove_path_block "$benchmark_home/$profile"
    done
    rm -f -- "$target"
    # Only dedicated cache directories are removed, never their parent.
    rm -rf -- "$benchmark_home/.cache/ps-benchmark"
    cache_parent=${XDG_CACHE_HOME:-}
    if [[ "$cache_parent" == /* ]]; then
        rm -rf -- "$cache_parent/ps-benchmark"
    fi
    printf 'ps-benchmark uninstalled; default diagnostic reports removed.\n'
    printf 'Open a new terminal to refresh the shell command cache.\n'
    printf 'Downloaded source files and reports saved to custom paths are user files; remove those separately.\n'
}

case "${1:-}" in
    -h|--help)
        printf 'Usage: bash install.sh [--no-run | --run [benchmark options] | --uninstall]\n'
        printf 'By default, installs and runs if ./push_swap is executable.\n'
        exit 0
        ;;
    --uninstall)
        [[ $# -eq 1 ]] || fail '--uninstall does not accept benchmark options.'
        uninstall
        exit 0
        ;;
    --no-run)
        run_mode=no
        shift
        [[ $# -eq 0 ]] || fail '--no-run does not accept benchmark options.'
        ;;
    --run)
        run_mode=yes
        shift
        ;;
    '') ;;
    *) fail 'Unknown installer option. Use --help.' ;;
esac

[[ "$benchmark_home" == /* ]] || fail 'The installation home must be an absolute path.'

compiler=
if [[ -n "${PS_BENCHMARK_CC:-}" ]]; then
    compiler=$(command -v "$PS_BENCHMARK_CC") || fail "Compiler not found: $PS_BENCHMARK_CC"
else
    for candidate in cc clang gcc; do
        if command -v "$candidate" >/dev/null 2>&1; then
            compiler=$(command -v "$candidate")
            break
        fi
    done
fi
[[ -n "$compiler" ]] || fail 'A C compiler is required (cc, clang or gcc). Install development tools and rerun.'

mkdir -p -- "$bin_dir"
[[ ! -d "$target" ]] || fail "$target is a directory."
build_dir=$(mktemp -d "$bin_dir/.ps-benchmark.XXXXXXXX")

# A downloaded script has no adjacent source. A local script can work offline.
script_source=${BASH_SOURCE[0]:-}
local_source=
if [[ -n "$script_source" && -f "$script_source" ]]; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$script_source")" && pwd)
    if [[ -f "$script_dir/push_swap_benchmark.c" ]]; then
        local_source="$script_dir/push_swap_benchmark.c"
    fi
fi
if [[ -n "$local_source" ]]; then
    printf 'Using local benchmark source.\n'
    cp -- "$local_source" "$build_dir/push_swap_benchmark.c"
else
    printf 'Downloading benchmark source...\n'
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --connect-timeout 10 --max-time 60 --retry 2 \
            "$PS_BENCHMARK_SOURCE_URL" -o "$build_dir/push_swap_benchmark.c" \
            || fail 'Could not download the source. Check the repository URL and your connection.'
    elif command -v wget >/dev/null 2>&1; then
        wget -q --timeout=60 --tries=3 -O "$build_dir/push_swap_benchmark.c" \
            "$PS_BENCHMARK_SOURCE_URL" \
            || fail 'Could not download the source. Check the repository URL and your connection.'
    else
        fail 'Downloading requires curl or wget. Alternatively, download the C file and compile it manually.'
    fi
fi

printf 'Compiling benchmark...\n'
"$compiler" -Wall -Wextra -Werror "$build_dir/push_swap_benchmark.c" \
    -lm -o "$build_dir/ps-benchmark" || fail 'Compilation failed. An existing installation was kept.'
chmod 755 "$build_dir/ps-benchmark"
"$build_dir/ps-benchmark" --help >/dev/null \
    || fail 'The compiled program could not run. An existing installation was kept.'
mv -f -- "$build_dir/ps-benchmark" "$target"

append_path_block() {
    local profile quoted_bin
    profile=$1
    if [[ -f "$profile" ]] && grep -Fq '# >>> ps-benchmark PATH >>>' "$profile"; then
        path_configured=1
        return
    fi
    printf -v quoted_bin '%q' "$bin_dir"
    if ! {
        printf '\n# >>> ps-benchmark PATH >>>\n'
        printf 'case ":$PATH:" in\n'
        printf '    *:%s:*) ;;\n' "$quoted_bin"
        printf '    *) export PATH=%s:"$PATH" ;;\n' "$quoted_bin"
        printf 'esac\n# <<< ps-benchmark PATH <<<\n'
    } >> "$profile"; then
        printf 'Could not update %s. Run the program using its full path:\n%s\n' \
            "$profile" "$target" >&2
        return
    fi
    printf 'Command PATH configured in %s.\n' "$profile"
    path_configured=1
}

configure_shell() {
    local shell_name profile candidate
    shell_name=${PS_BENCHMARK_SHELL:-${SHELL:-}}
    shell_name=${shell_name##*/}
    case "$shell_name" in
        bash)
            append_path_block "$benchmark_home/.bashrc"
            # Keep the login profile that Bash already uses, including .profile.
            profile="$benchmark_home/.bash_profile"
            for candidate in .bash_profile .bash_login .profile; do
                if [[ -f "$benchmark_home/$candidate" ]]; then
                    profile="$benchmark_home/$candidate"
                    break
                fi
            done
            append_path_block "$profile"
            ;;
        zsh) append_path_block "$benchmark_home/.zshrc" ;;
        *)
            printf 'Shell PATH setup is automatic for Bash and Zsh.\n'
            printf 'Add this directory to your shell PATH: %s\n' "$bin_dir"
            ;;
    esac
}

configure_shell
printf '\nInstalled: %s\n' "$target"
case ":${PATH:-}:" in
    *:"$bin_dir":*) printf 'Next time, run: ps-benchmark\n' ;;
    *)
        if [[ "$path_configured" == 1 ]]; then
            printf 'For future runs, open a new terminal and type: ps-benchmark\n'
        fi
        printf 'To run in this terminal immediately: %q\n' "$target"
        ;;
esac

cleanup
trap - EXIT
if [[ "$run_mode" == yes || ( "$run_mode" == auto && -x ./push_swap && ! -d ./push_swap ) ]]; then
    printf '\nStarting benchmark for ./push_swap...\n'
    exec "$target" "$@"
else
    printf 'Run ps-benchmark from a folder containing your compiled push_swap.\n'
fi
