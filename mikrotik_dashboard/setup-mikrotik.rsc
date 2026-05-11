# Configuracion del Mikrotik hAP ax3 para el dashboard del ESP-32.
#
# Crea un grupo de solo lectura, un usuario dedicado y habilita el
# servicio "www" (REST API de RouterOS v7 por HTTP). Es idempotente: si
# el grupo o el usuario ya existen, los actualiza en lugar de duplicarlos.
#
# ---------------------------------------------------------------------------
# Uso
# ---------------------------------------------------------------------------
# 1) Edita las variables de abajo (al menos dashPass).
# 2) Sube el fichero al router:
#    a. Arrastrandolo a Files en WinBox/WebFig, o
#    b. Por SSH: scp setup-mikrotik.rsc admin@192.168.88.1:/
#    c. O desde el propio router:
#       /tool/fetch url=http://<host>/setup-mikrotik.rsc mode=http
# 3) Ejecutalo:  /import file-name=setup-mikrotik.rsc
#
# Alternativa: copiar y pegar este contenido en la terminal del router.
# ---------------------------------------------------------------------------
# Requisitos: RouterOS v7.1beta4 o superior (REST API incluida).
# ---------------------------------------------------------------------------

:local dashUser   "dashboard"
:local dashPass   "CAMBIA_ESTE_PASSWORD"
:local dashGroup  "readonly"
# Deja en "" para no restringir la IP cliente. Si no, p.ej. "192.168.88.50/32".
:local dashClient ""

:put "==> Dashboard setup starting"

# 1. Grupo de solo lectura ----------------------------------------------------
:if ([:len [/user/group find name=$dashGroup]] = 0) do={
    /user/group add name=$dashGroup \
        policy=read,api,rest-api,!write,!policy,!sensitive \
        comment="ESP-32 dashboard read-only"
    :put ("  + grupo '" . $dashGroup . "' creado")
} else={
    /user/group set [find name=$dashGroup] \
        policy=read,api,rest-api,!write,!policy,!sensitive
    :put ("  = grupo '" . $dashGroup . "' ya existia (policy actualizada)")
}

# 2. Usuario dedicado ---------------------------------------------------------
:if ([:len [/user find name=$dashUser]] = 0) do={
    /user add name=$dashUser group=$dashGroup password=$dashPass \
        comment="ESP-32 dashboard"
    :put ("  + usuario '" . $dashUser . "' creado")
} else={
    /user set [find name=$dashUser] group=$dashGroup password=$dashPass
    :put ("  = usuario '" . $dashUser . "' actualizado")
}

# 3. Servicio www (REST API sobre HTTP) ---------------------------------------
/ip/service enable www
:if ($dashClient != "") do={
    /ip/service set www address=$dashClient
    :put ("  + servicio www restringido a " . $dashClient)
} else={
    /ip/service set www address=""
    :put "  = servicio www habilitado sin restriccion de IP"
}

:put "==> Dashboard setup done"
:put "    Prueba desde un PC de la LAN:"
:put ("    curl -u " . $dashUser . ":" . $dashPass . \
      " http://[IP-ROUTER]/rest/system/resource")
:put "    Si devuelve un JSON con cpu-load y free-memory, ya puedes"
:put "    flashear el ESP-32 con el sketch mikrotik_dashboard."
