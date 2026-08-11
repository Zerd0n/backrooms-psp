#!/usr/bin/env bash
set -Eeuo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
log_dir="${project_root}/logs"
timestamp="$(date '+%Y%m%d_%H%M%S')"
log_file="${log_dir}/build_${timestamp}.log"
docker_image="${BACKROOMS_DOCKER_IMAGE:-pspdev/pspdev:latest}"
docker_config="${BACKROOMS_DOCKER_CONFIG:-${project_root}/tools/docker-config}"
active_container=""

mkdir -p "${log_dir}"
exec > >(tee -a "${log_file}") 2>&1

on_error() {
    local exit_code=$?
    cleanup_container
    echo "[ERROR] Build failed with exit code ${exit_code}. See ${log_file}"
    exit "${exit_code}"
}
trap on_error ERR

cleanup_container() {
    if [[ -n "${active_container}" ]]; then
        env DOCKER_CONFIG="${docker_config}" docker rm -f "${active_container}" >/dev/null 2>&1 || true
        active_container=""
    fi
}

docker_make() {
    local target=$1
    local container_id
    local exit_code
    active_container="backrooms_psp_${target}_${timestamp}_$$"
    container_id="$(env DOCKER_CONFIG="${docker_config}" docker run -d --platform linux/amd64 \
        --name "${active_container}" -v "${project_root}:/src" -w /src \
        "${docker_image}" make "${target}")"
    echo "[INFO] Container ${container_id} running make ${target}"
    exit_code="$(env DOCKER_CONFIG="${docker_config}" docker wait "${active_container}")"
    env DOCKER_CONFIG="${docker_config}" docker logs "${active_container}"
    env DOCKER_CONFIG="${docker_config}" docker rm "${active_container}" >/dev/null
    active_container=""
    if [[ "${exit_code}" != "0" ]]; then
        echo "[ERROR] make ${target} exited with ${exit_code}"
        return 1
    fi
}

retry() {
    local attempt=1
    local max_attempts=3
    local delay=2
    while true; do
        echo "[INFO] Attempt ${attempt}/${max_attempts}: $*"
        if "$@"; then
            return 0
        fi
        if (( attempt >= max_attempts )); then
            return 1
        fi
        echo "[WARN] Command failed; retrying in ${delay}s"
        sleep "${delay}"
        attempt=$((attempt + 1))
        delay=$((delay * 2))
    done
}

if ! command -v docker >/dev/null 2>&1; then
    echo "[ERROR] Docker is not installed or is not in PATH."
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "[ERROR] Python 3 is required for asset preprocessing and verification."
    exit 2
fi
if [[ ! -f "${docker_config}/config.json" ]]; then
    echo "[ERROR] Docker config not found: ${docker_config}/config.json"
    exit 2
fi

echo "[INFO] Backrooms PSP clean build started"
echo "[INFO] Project: ${project_root}"
echo "[INFO] Image: ${docker_image}"
cd "${project_root}"
python3 tools/convert_textures.py
python3 tools/convert_audio.py
python3 tools/generate_ambient.py
retry docker_make clean
retry docker_make all

cd "${project_root}"
python3 tools/verify_project.py
echo "[INFO] Build and verification succeeded: ${project_root}/EBOOT.PBP"
echo "[INFO] Log: ${log_file}"
