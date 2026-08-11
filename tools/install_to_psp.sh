#!/usr/bin/env bash
set -Eeuo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_source="${project_root}/dist/BACKROOMS3D"
timestamp="$(date '+%Y%m%d_%H%M%S')"
log_dir="${project_root}/logs"
mkdir -p "${log_dir}"
log_file="${log_dir}/install_${timestamp}.log"
exec > >(tee -a "${log_file}") 2>&1

usage() {
    echo "Usage: $0 /Volumes/PSP"
    echo "       $0 --rollback /Volumes/PSP BACKROOMS3D.backup.YYYYMMDD_HHMMSS"
}

validate_mount() {
    local mount_path=$1
    if [[ "${mount_path}" != /* || ! -d "${mount_path}" ]]; then
        echo "[ERROR] PSP mount path must be an existing absolute directory: ${mount_path}"
        exit 2
    fi
}

if [[ "${1:-}" == "--rollback" ]]; then
    if [[ $# -ne 3 ]]; then usage; exit 2; fi
    mount_root=$2
    backup_name=$3
    validate_mount "${mount_root}"
    if [[ ! "${backup_name}" =~ ^BACKROOMS3D\.backup\.[0-9]{8}_[0-9]{6}$ ]]; then
        echo "[ERROR] Invalid backup folder name: ${backup_name}"
        exit 2
    fi
    game_root="${mount_root}/PSP/GAME"
    target="${game_root}/BACKROOMS3D"
    backup="${game_root}/${backup_name}"
    if [[ ! -d "${backup}" ]]; then
        echo "[ERROR] Backup does not exist: ${backup}"
        exit 2
    fi
    if [[ -e "${target}" ]]; then
        mv "${target}" "${game_root}/BACKROOMS3D.replaced.${timestamp}"
    fi
    mv "${backup}" "${target}"
    echo "[INFO] Rollback complete: ${target}"
    exit 0
fi

if [[ $# -ne 1 ]]; then usage; exit 2; fi
mount_root=$1
validate_mount "${mount_root}"
if [[ ! -s "${release_source}/EBOOT.PBP" || ! -s "${release_source}/assets/chase.raw" ]]; then
    echo "[ERROR] Release package is missing. Run tools/build.sh or make verify first."
    exit 2
fi

game_root="${mount_root}/PSP/GAME"
target="${game_root}/BACKROOMS3D"
backup="${game_root}/BACKROOMS3D.backup.${timestamp}"
mkdir -p "${game_root}"
if [[ -e "${target}" ]]; then
    mv "${target}" "${backup}"
    echo "[INFO] Existing installation backed up to ${backup}"
fi
if ! cp -R "${release_source}" "${target}"; then
    echo "[ERROR] Copy failed. Restoring previous installation if available."
    if [[ -d "${backup}" && ! -e "${target}" ]]; then
        mv "${backup}" "${target}"
    fi
    exit 1
fi
sync
echo "[INFO] Installed to ${target}"
echo "[INFO] Safely eject the PSP before disconnecting USB."
echo "[INFO] Log: ${log_file}"
