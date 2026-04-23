# NODOSIMPMod
Tratamento de arquivos para impressão em sistema legado.

Aplicação criada para integrar sistema legado (feito em clipper) a impressão bi-direcional sem nescessidade de alteração no fonte original do sistema por falta de acesso.

A aplicação recebe paramêtros de inicialização, e tem comportamentos diferentes para cada tipo de chamada:

# NODOSIMP.EXE CAMINHO_TXT 140 -> 

Abre o arquivo de texto, remove caracteres indesejados, verifica se existe um dos identificadores de modelo de impressão para impressora térmica, caso tenha, imprime diretamente na impressora configurada.

# NODOSIMP.EXE CAMINHO_TXT 140 /PRE/VER/SEL ->  

Abre o arquivo de texto, remove caracteres indesejados, e chama aplicação externa "NODOSIMP2.exe" com os mesmos parâmetros da chamada.

# NODOSIMP.EXE -> 

Se for chamado sem paramêtro, inicializa a GUI de configuração da impressora, fonte e BMP(Logo).

# Config

Arquivo de configuração é criado por usuário local em "%APPDATA%/NODOSIMP/nodosimp.ini" caso não exista config, abre a GUI para configuração inicial.

# Exceções

Caso for solicitado a impressão, e a impressora não estiver disponível, abre a GUI para seleção de uma impressora válida.

Caso exista a pasta "C:\WINPRINT\MATRICIAL" a chamada da impressão é enviada direto para a porta LPT1.

Caso exista o arquivo "NODOSIMP.SEL" no mesmo diretório do EXE, a chamada para NODOSIMP2 acrescenta o paramêtro "/SEL" para selecionar qual impressora fará a impressão.

# Futuro

Pretendo futuramente remover todas as chamadas de "NODOSIMP2.EXE" para funções dentro da própria aplicação, para funcionar de maneira 100% autônoma.
