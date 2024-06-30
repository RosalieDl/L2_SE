// *******************************************************
// Nom ......... : mon-shell
// Rôle ........ : Shell maison
// Auteur ...... : Rosalie Duteuil
// Version ..... : V6.0 du 30/06/2024
// Licence ..... : réalisé dans le cadre du cours de Systèmes d'Exploitaiton
// Compilation . : gcc -Wall shellv2.c -o mon-shell -lreadline
// Usage ....... : Pour exécuter : ./mon-shell
// ********************************************************/

#include "sys.h"
#include <string.h>
#include <ctype.h>		// pour isspace() 
#include <stdbool.h>
#include <readline/readline.h>

int execute(char *);
int lance_cmd(char *[]);
int exec_pipeline(char *[]);
bool cmd_internes(char * []);
int moncd(char *[]);
int check_redir(char **);
int redirige(char *, char *);
int decouper(char *, char *, char *[], int);
bool premier_plan(char *);
bool est_vide(char *);
int chiffre(char);

enum {
	MaxLigne = 1024, 			// longueur max d'une ligne de commandes
	MaxMot = MaxLigne / 2, 		// nbre max de mot dans la ligne
	MaxDirs = 100, 				// nbre max de repertoires dans PATH
	MaxPathLength = 512, 		// longueur max d'un nom de fichier
	MaxPipes = MaxMot / 2,		// nbre max de commandes séparées par " | " sur une ligne
};

# define PROMPT "|> "
char * path[MaxDirs];			// liste des répertoires de PATH

int main(int argc, char * argv[]){
	char * ligne = NULL;
	int retour;

	/* Découper UNE COPIE de PATH en repertoires */
	decouper(strdup(getenv("PATH")), ":", path, MaxDirs);

	/* Lire et traiter chaque ligne saisie dans le terminal */
	for ( ; (ligne = readline(PROMPT)) ; ){

		/* Exécuter la commande */
		if (!est_vide(ligne))
			execute(ligne);

		/* Récupérer les éventuels processus lancés en arrière-plan qui sont terminés */
		while ((retour = waitpid(-1, 0, WNOHANG)) > 0)
			if (retour > 0 ) printf("[Processus %i terminé]\n", retour);
		
		free(ligne);
	}
	return 0;
}

/* Exécution d'un ligne entière saisie par l'utilisateur */
int execute(char * ligne){
	char * listePipes[MaxPipes];		// la ou les (en cas de pipe(s)) commande(s) de la ligne
	char * mots[MaxMot];				// liste des mots de la commande à exécuter
	int pid;

	bool fg = premier_plan(ligne);		// exécution en premier plan (true) ou arrière plan (false)
			
	/* si consiste en plusieurs commandes séparées par des pipes */ 
	if (strchr(ligne, '|') != NULL){
		decouper(ligne, "|", listePipes, MaxPipes);
		pid = exec_pipeline(listePipes); 	// pid du dernier processus de la pipeline
		if (pid < 0) return -1; }
	
	/* si la commande est unique */
	else {
		decouper(ligne, " \t\n", mots, MaxMot);
		
		/* commande interne --> exécution directement depuis le shell */ 
		if (fg && cmd_internes(mots))
			return 0;

		/* sinon, lancement du processus fils */
		if ((pid = fork()) < 0){
			perror("fork");
			return -1;}

		/* enfant : exécute la commande*/
		if (pid == 0)
			lance_cmd(mots);
	}

	/* parent : attendre la fin de l'enfant (ou pas) */
	if (!fg) printf("bg : %d\n", pid);		// bg : on affiche le PID du fils
		
	else {									// fg : on attend la fin du fils
		if (waitpid(pid, 0, 0) < 0){		
			perror("wait");
			return -1; }
	}
	return 0;
}

/* exécute une commande, en cherchant l'exécutable dans la liste de répertoires fournie */
int lance_cmd(char * commande[]){	
	char pathname[MaxPathLength];
	
	if (cmd_internes(commande)) exit(0);	// c'est une commande interne et elle a été exécutée --> on sort
	check_redir(commande);					// mise en place des éventuelles redirections d'E/S

	/* recherche de la commande dans le PATH */
	for (int i = 0; path[i] != 0; i ++){
		snprintf(pathname, sizeof pathname, "%s/%s", path[i], commande[0]);
		execv(pathname, commande);
	}
	fprintf(stderr, "%s: not found\n", commande[0]);
	exit(-1);
}

/* **************************************************************** */
/*				Traitement des pipes								*/
/* **************************************************************** */

/* Exécute une pipeline (une série de commandes séparées par des "|" ) */
int exec_pipeline(char * pipeline[]){
	int i, pid;
	char * mots[MaxMot];				
	
	/* lancement du sous-shell qui va gérer la pipeline */
	if ((pid = fork()) < 0){
		perror("fork");
		return -1;}

	/* [parent] : renvoyer le PID du "sous-shell" */
	if (pid > 0) return pid;

	/* [enfant] : prend en charge la pipeline */
	int pipe_fd[2];				
	int cmd;
	int in = 0;

	for (i = 0; pipeline[i+1] != 0; i++) {	
		if (est_vide(pipeline[i])){
			fprintf(stderr, "Erreur de syntaxe (pipe vide)\n");
			exit(-1);}

		/* ouvrir un pipe pour communiquer avec le processus suivant */	
		if (pipe(pipe_fd) != 0) {
			perror("pipe");
			exit(-1);}

		/* créer un enfant pour exécuter la "sous-commande" */
		if ((cmd = fork()) < 0){
			perror("fork");
			exit(-1);}

		/* Enfant */
		if (cmd == 0){
			close(pipe_fd[0]); 		// ferme la sortie du tube (inutilisée)
			dup2(pipe_fd[1], 1); 	// redirige sa sortie sur l'entrée du tube
			close(pipe_fd[1]);
			dup2(in, 0);			// redirige la sortie utilisée précédemment sur son entrée
			close(in);
			decouper(pipeline[i], " \t\n", mots, MaxMot);
			lance_cmd(mots);		// exécute la commande
		}

		/* Parent */
		close(pipe_fd[1]);			// fermeture de l'entrée du tube actuel
		if (in != 0) close(in);		
		in = pipe_fd[0];
	}

	dup2(in, 0);		// récupère en entrée la sortie du dernier pipe
	close(in); 

	/* dernière commande de la pipeline */
	if (!est_vide(pipeline[i])){
		decouper(pipeline[i], " \t\n", mots, MaxMot);
		lance_cmd(mots);
	}
	exit(0);
}

/* **************************************************************** */
/*		Gestion des commmandes internes								*/
/* **************************************************************** */

/* si l'entrée correspond à  une commande interne, l'exécuter et renvoyer 0, sinon 1 */
bool cmd_internes(char * commande[]){
	
	/* commande cd perso*/
	if (strcmp("moncd", commande[0]) == 0){
		moncd(commande);
		return true; }

	/* [...] Autres éventuelles commandes internes [...] */ 

	/* commande exit perso */
	else if (strcmp("monexit", commande[0]) == 0)
		exit(0);

	return false;
}

/* change de répertoire courant */
int moncd(char * commande[]){
	char * dir;
	int len;

	for (len = 0; commande[len] != 0; len ++);	// nombre de mots

	if (len < 2){
		dir = getenv("HOME");
		if (dir == 0) dir = "/tmp";}
	else if (len > 2){
		fprintf(stderr, "usage: %s [dir]\n", commande[0]);
		return 1;}
	else 
		dir = commande[1];
	
	if (chdir(dir) < 0){
		perror(dir);
		return 1;}
	return 0;
}

/* **************************************************************** */
/*				Gestion des redirections 							*/
/* **************************************************************** */

/* Parcourt un vecteur de mots pour en extraire la (ou les) redirection(s) */
int check_redir(char * mots[]){
	int i = 0;			// index pour recopier les mots non liés à la redirection
	
	for(int k = 0; mots[k] ; k++) {	
		/* si c'est un potentiel opérateur de redirection (contient les symboles < ou >) */
		if (strchr(mots[k], '<') != NULL || strchr(mots[k], '>') != NULL) {	
			
			/* opérateur à trois symboles max (redirection vers/depuis un fichier) */
			if (strlen(mots[k]) < 4) {		
				if (mots[k+1] == NULL) { 		// dernier mot de la ligne
					fprintf(stderr, "Echec de la redirection \"%s\" : manque le nom du fichier.\n", mots[k]);
					continue; }			
				redirige(mots[k], mots[k+1]);	// faire la redirection
				k++; }							// ignorer le mot suivant (nom du fichier)

			/* opérateur type 2>&1 (redirections entre flux standards) */
			else redirige(mots[k], NULL);		// faire la redirection
		}
		else mots[i++] = mots[k];				// ne recopier que les autres mots
	}
	mots[i]=NULL;								// mettre à jour la fin du vecteur
	return 0; }

/* Réalise une redirection basique (<, >, >>, 2>, 2>>, 2>&1, ou 1>&2) */
int redirige(char * op, char * fichier){	
	int stream;		// descripteur du fichier du/vers lequel on redirige
	int fd;			// flux standard concerné par la redirection (0, 1 ou 2)

	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // permissions pour la création de fichier, ici = 644

	if (strcmp(op, "<") == 0) {				// lecture depuis un fichier
		stream = open(fichier, O_RDONLY);
		fd = 0; }
	else if (strcmp(op, ">") == 0) {		// écriture de stdout dans un fichier (écrasement)
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 1; }
	else if (strcmp(op, ">>") == 0){		// écriture de stdout dans un fichier (concaténation)
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 1; }
	else if (strcmp(op, "2>") == 0){		// écriture de stderr dans un fichier (écrasement)
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 2; }
	else if (strcmp(op, "2>>") == 0){		// écriture de stderr dans un fichier (concaténation)
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 2; }
	else if (op[1] == '>' && op[2] == '&'){	// opérateur de type 2>&1
		fd = chiffre(op[0]);					// flux à rediriger (premier entier de l'opérateur)
		stream = chiffre(op[3]);}				// flux vers lequel rediriger (second entier)
	else {									// si rien de tout ça
		fprintf(stderr, "Erreur : redirection \"%s\" non reconnue.\n", op);
		return -1; }
	
	if (stream < 0) {				// erreur dans l'ouverture du fichier
			perror("open");
			return -1; }
	
	dup2(stream, fd);				// redirection
	if (stream > 2) close(stream);	// fermeture du fichier (hors cas de fusion de flux standards type 2>&1)
	return 0;
}

/* **************************************************************** */
/*				Tests et traitements divers							*/
/* **************************************************************** */

/* Découpe une chaîne de caractères et renvoie le nombre de mots récupérés */
int decouper(char * ligne, char * separ, char * mot[], int maxmot){	
	int i ;
	mot[0] = strtok(ligne, separ);
	for(i = 1; mot[i-1] != 0; i ++){
		if (i == maxmot){
			fprintf (stderr, "Erreur dans la fonction decouper: trop de mots\n");
			mot[i-1] = 0;
			break;
		}
		mot[i] = strtok(NULL, separ);
	}
	return (i-1);
}

/* Renvoie true ssi la commande doit être exécutée au premier plan */
bool premier_plan(char * ligne){
	char *c = strrchr(ligne, '&');		// trouve le dernier '&' contenu dans la ligne
	if (c == NULL || isalnum(*(c+1)))	// pas de '&' dans la ligne, ou alors fait partie d'un mot (ex redirection "2>&1")
		return true;
	*c = '\0';							// éliminer le '&'
	return false;
}

/* Vérifie si une chaîne de caractère est vide (ne contient que des espaces) */
bool est_vide(char *str) {
    while (*str){
        if (!isspace(*str))
            return false;
        str++; }
    return true;
}

/* Convertit un caractère [0-9] en l'entier correspondant */
int chiffre(char c) {return c - '0';}
