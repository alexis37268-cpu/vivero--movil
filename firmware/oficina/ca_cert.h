// =============================================================
//  ca_cert.h  -  Certificado de la autoridad (CA) del broker
//
//  Sirve para que el ESP32 verifique que esta hablando con el
//  broker de verdad y no con un impostor (TLS).
//
//  Como completarlo:
//   1) EMQX -> deployment "vivero" -> Overview -> CA Certificate
//      (descarga el archivo emqxsl-ca.crt)
//   2) Abrirlo con el Bloc de notas
//   3) Pegar TODO el contenido (con las lineas BEGIN y END)
//      reemplazando las 3 lineas de abajo.
//
//  Es un certificado publico: no es secreto, se puede subir a GitHub.
// =============================================================
#pragma once

static const char CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
PEGAR_ACA_EL_CERTIFICADO
-----END CERTIFICATE-----
)EOF";
