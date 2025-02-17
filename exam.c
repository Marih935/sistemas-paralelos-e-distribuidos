#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#define MAX_LINHA 1024
#define MAX_CANDIDATOS 10000
#define NUM_QUESTOES 30
#define MEDIA_APROVACAO 6.0  // Média para ser aprovado

typedef struct {
    char id[20];
    char cargo[10];
    char respostas[NUM_QUESTOES];  // Vetor de char para armazenar as respostas
    float nota_lp, nota_ml, nota_especifica, media_final;
    int aprovado;
} Candidato;

// Armazena a contagem de acertos por questão
int acertos_questoes[NUM_QUESTOES] = {0};

// Função para carregar o gabarito do CSV
void carregar_gabarito(char gabarito[NUM_QUESTOES][2]) {
    FILE *file = fopen("gabarito.csv", "r");
    if (!file) {
        printf("Erro ao abrir gabarito.csv\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    char linha[MAX_LINHA];
    if (fgets(linha, MAX_LINHA, file) != NULL) {
        char *token = strtok(linha, ",");
        for (int i = 0; i < NUM_QUESTOES && token != NULL; i++) {
            strcpy(gabarito[i], token);
            token = strtok(NULL, ",");
        }
    }
    fclose(file);
    printf("Gabarito carregado com sucesso!\n");
}

// Função para carregar candidatos do CSV em blocos
int carregar_candidatos(Candidato candidatos[], int rank, int size) {
    FILE *file = fopen("respostas.csv", "r");
    if (!file) {
        printf("Erro ao abrir respostas.csv\n");
        return 0;
    }

    char linha[MAX_LINHA];
    int count = 0;
    int total_linhas = 0;

    // Contar o número total de linhas no arquivo
    while (fgets(linha, MAX_LINHA, file)) {
        total_linhas++;
    }
    rewind(file);  // Volta para o início do arquivo após contar as linhas

    int candidatos_por_processo = total_linhas / size;
    int resto = total_linhas % size;
    int inicio = rank * candidatos_por_processo + (rank < resto ? rank : resto);
    int fim = inicio + candidatos_por_processo + (rank < resto ? 1 : 0);

    // Processar apenas as linhas que o processo precisa
    int linha_atual = 0;
    while (fgets(linha, MAX_LINHA, file)) {
        if (linha_atual >= inicio && linha_atual < fim) {
            char *id = strtok(linha, ",");
            char *cargo = strtok(NULL, ",");
            char *respostas = strtok(NULL, "\n");

            if (id && cargo && respostas) {
                // Remover aspas se necessário
                if (cargo[0] == '"') memmove(cargo, cargo + 1, strlen(cargo));
                if (cargo[strlen(cargo) - 1] == '"') cargo[strlen(cargo) - 1] = '\0';

                if (strcmp(cargo, "1508") == 0) {
                    strcpy(candidatos[count].id, id);
                    strcpy(candidatos[count].cargo, cargo);
                    char *token = strtok(respostas, ",");
                    for (int i = 0; i < NUM_QUESTOES && token != NULL; i++) {
                        candidatos[count].respostas[i] = token[0];
                        token = strtok(NULL, ",");
                    }
                    count++;
                }
            }
        }
        linha_atual++;
    }

    fclose(file);
    return count;
}

// Função para calcular as notas
void calcular_notas(Candidato *candidato, char gabarito[NUM_QUESTOES][2]) {
    candidato->nota_lp = 0;
    candidato->nota_ml = 0;
    candidato->nota_especifica = 0;

    for (int i = 0; i < NUM_QUESTOES; i++) {
        if (candidato->respostas[i] == gabarito[i][0]) {
            if (i < 10) {  // Língua Portuguesa
                candidato->nota_lp += 1.0;
            } else if (i < 20) {  // Matemática e Lógica
                candidato->nota_ml += 1.0;
            } else {  // Específica
                candidato->nota_especifica += 1.0;
            }
        }
    }

    // Calculando a média final
    candidato->media_final = (candidato->nota_lp + candidato->nota_ml + candidato->nota_especifica) / 3.0;

    // Definindo aprovação (média maior ou igual a MEDIA_APROVACAO)
    candidato->aprovado = (candidato->media_final >= MEDIA_APROVACAO) ? 1 : 0;
}

// Função para imprimir resultados
void imprimir_resultados(Candidato candidatos[], int total_candidatos, int rank) {
    int qtd_aprovados = 0;
    float soma_media = 0;

    // Exibir os dados dos candidatos
    for (int i = 0; i < total_candidatos; i++) {
        printf("ID: %s, Nota LP: %.2f, Nota ML: %.2f, Nota Específica: %.2f, Média Final: %.2f, Resultado: %s\n",
            candidatos[i].id, candidatos[i].nota_lp, candidatos[i].nota_ml, candidatos[i].nota_especifica,
            candidatos[i].media_final, candidatos[i].aprovado ? "Aprovado" : "Reprovado");

        // Calcular total de aprovados e média geral
        if (candidatos[i].aprovado) {
            qtd_aprovados++;
        }
        soma_media += candidatos[i].media_final;
    }

    // Média geral dos candidatos
    float media_geral = soma_media / total_candidatos;
    printf("\nTotal de Candidatos: %d, Candidatos Aprovados: %d, Média Geral dos Candidatos: %.2f\n\n",
            total_candidatos, qtd_aprovados, media_geral);

    // Exibir lista de candidatos aprovados
    printf("Lista de Candidatos Aprovados:\n");
    for (int i = 0; i < total_candidatos; i++) {
        if (candidatos[i].aprovado) {
            printf("ID: %s\n", candidatos[i].id);
        }
    }
}

int main(int argc, char *argv[]) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char gabarito[NUM_QUESTOES][2];
    Candidato candidatos[MAX_CANDIDATOS];
    int total_candidatos = 0;

    if (rank == 0) {
        carregar_gabarito(gabarito);
        total_candidatos = carregar_candidatos(candidatos, rank, size);
        printf("Total de candidatos filtrados para cargo 1508: %d\n", total_candidatos);
    }

    // Distribuir número total para todos os processos
    MPI_Bcast(&total_candidatos, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (total_candidatos == 0) {
        printf("Nenhum candidato encontrado. Finalizando.\n");
        MPI_Finalize();
        return 0;
    }

    // Dividir trabalho entre processos
    int candidatos_por_processo = total_candidatos / size;
    int resto = total_candidatos % size;
    int inicio = rank * candidatos_por_processo + (rank < resto ? rank : resto);
    int fim = inicio + candidatos_por_processo + (rank < resto ? 1 : 0);

    printf("Processo %d: Processando candidatos de %d a %d\n", rank, inicio, fim - 1);

    // Calcular notas para cada candidato
    for (int i = inicio; i < fim; i++) {
        calcular_notas(&candidatos[i], gabarito);
    }

    // Sincronizar os resultados
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        imprimir_resultados(candidatos, total_candidatos, rank);
    }

    MPI_Finalize();
    return 0;
}
